#!/usr/bin/env python3
"""Offline ADC3101 filter design/source audit. Never opens hardware or saves audio.

Register/transfer-function reference: TI SLAS553B sections 10.3.4/10.3.12.3.
The candidate replaces only Tater's software 80 Hz one-pole, not the existing
vendor biquad, decimator, AGC policy or per-unit calibration.
"""
from __future__ import annotations

import argparse
import cmath
import hashlib
import json
import math
from pathlib import Path
import re

RATE = 16000
CORNER = 80
Q15 = 32768
DRIVER_SHA256 = "4d05170eb53df0a2b5a4c98b791ba2a18e667e38d2a18f3b1cda36e5bafde1b6"
HEADER_SHA256 = "6c5e57d6132f1de8619a4afa51bc96f55e9bd45b4058abcffc2abb3bc178ebb2"
FREQUENCIES = (0, 10, 20, 40, 80, 100, 200, 300, 1000, 4000, 7999)


def signed16(value: int) -> int:
    if not 0 <= value <= 65535:
        raise ValueError("not a 16-bit word")
    return value if value < 32768 else value - 65536


def quantized_highpass() -> tuple[int, int, int]:
    """N0,N1,D1 for H(z)=(N0+N1*z^-1)/(32768-D1*z^-1).

    Match Processor.HighPass's exponential pole. Round the pole, then round
    the Nyquist-normalized numerator downward to avoid any passband boost.
    N1=-N0 preserves an exact DC zero after quantization.
    """
    pole = round(math.exp(-2 * math.pi * CORNER / RATE) * Q15)
    n0 = (Q15 + pole) // 2
    return n0, -n0, pole


def coefficient_bytes(words: tuple[int, ...]) -> bytes:
    result = bytearray()
    for word in words:
        if not -32768 <= word <= 32767:
            raise ValueError("coefficient outside signed Q1.15")
        result.extend((word & 65535).to_bytes(2, "big"))
    return bytes(result)


def one_pole_response(words: tuple[int, int, int], hz: float) -> complex:
    if not 0 <= hz <= RATE / 2:
        raise ValueError("frequency outside capture Nyquist range")
    z = cmath.exp(-2j * math.pi * hz / RATE)
    n0, n1, d1 = words
    return (n0 + n1 * z) / (Q15 - d1 * z)


def software_response(hz: float) -> complex:
    alpha = math.exp(-2 * math.pi * CORNER / RATE)
    z = cmath.exp(-2j * math.pi * hz / RATE)
    return ((1 + alpha) / 2) * (1 - z) / (1 - alpha * z)


def biquad_response(words: tuple[int, int, int, int, int], hz: float) -> complex:
    n0, n1, n2, d1, d2 = words
    z = cmath.exp(-2j * math.pi * hz / RATE)
    return (n0 + 2 * n1 * z + n2 * z * z) / (Q15 - 2 * d1 * z - d2 * z * z)


def db(value: complex) -> float | None:
    amplitude = abs(value)
    return None if amplitude == 0 else round(20 * math.log10(amplitude), 6)


def candidate() -> dict:
    words = quantized_highpass()
    errors = []
    for i in range(1, 8001):
        desired = software_response(i)
        actual = one_pole_response(words, i)
        errors.append(abs(20 * math.log10(abs(actual / desired))))
    return {
        "schema": 1,
        "kind": "offline_candidate_not_installed",
        "rate_hz": RATE,
        "corner_hz": CORNER,
        "first_order_q15": list(words),
        "own_coefficient_bytes_hex": coefficient_bytes(words).hex(),
        "pole_magnitude": words[2] / Q15,
        "dc_zero_exact": words[0] + words[1] == 0,
        "maximum_response_error_db_1hz_to_nyquist": max(errors),
        "response": [
            {"hz": hz, "candidate_db": db(one_pole_response(words, hz)),
             "software_db": db(software_response(hz))}
            for hz in FREQUENCIES
        ],
        "registers": {"page": 4, "left_first_order_start": 8, "right_first_order_start": 72,
                      "bytes_per_channel": 6},
        "hardware_writes": 0,
        "qualification": "math only; no live frequency response, AEC or voice qualification",
    }


def audit_source(driver: bytes, header: bytes, target: str) -> dict:
    """Inspect only the pinned public GPL source; never emit its coefficients."""
    if target not in ("biscuit", "radar"):
        raise ValueError("unqualified target")
    driver_sha = hashlib.sha256(driver).hexdigest()
    header_sha = hashlib.sha256(header).hexdigest()
    if (driver_sha, header_sha) != (DRIVER_SHA256, HEADER_SHA256):
        raise ValueError("unexpected driver/header hash; review new source before analyzing")
    source = driver.decode("utf-8")
    match = re.search(r"biquad_settings\[\]\s*=\s*\{(.*?)\n\};", source, re.S)
    if not match:
        raise ValueError("normal tuning table not found")
    registers = {}
    for address, value in re.findall(r"\{\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\}", match[1]):
        address, value = int(address, 16), int(value, 16)
        if address in registers or value > 255:
            raise ValueError("ambiguous tuning write")
        registers[address] = value
    channel_payloads = []
    channel_responses = []
    for adc in range(4):
        for offset in (14, 78):
            addresses = [4 * 2048 + adc * 256 + offset + i for i in range(10)]
            if not all(address in registers for address in addresses):
                raise ValueError("missing expected four-ADC stereo biquad")
            payload = bytes(registers[address] for address in addresses)
            channel_payloads.append(payload)
            words = tuple(signed16(int.from_bytes(payload[i:i+2], "big")) for i in range(0, 10, 2))
            channel_responses.append(biquad_response(words, 1000))
    # First-order memory defaults to TI's flat coefficients after reset; the
    # normal source table must leave it alone for the proposed addition.
    first_order_addresses = {
        4 * 2048 + adc * 256 + offset + i
        for adc in range(4) for offset in (8, 72) for i in range(6)
    }
    untouched_first_order = not any(address in registers for address in first_order_addresses)
    prb_two = bool(re.search(r"snd_soc_write\(codec,\s*ADC_PRB_SELECT\(i\),\s*0x2\)", source))
    if not prb_two or not untouched_first_order:
        raise ValueError("processing-block/first-order assumptions changed")
    return {
        "schema": 1,
        "kind": "public_source_audit_not_physical_register_proof",
        "target": target,
        "driver_sha256": driver_sha,
        "header_sha256": header_sha,
        "adc_count": 4,
        "normal_tuning_channel_count": len(channel_payloads),
        "normal_stereo_biquads_identical": len(set(channel_payloads)) == 1,
        "normal_biquad_payload_sha256": hashlib.sha256(b"".join(channel_payloads)).hexdigest(),
        "normal_biquad_gain_at_1khz_db": [db(value) for value in channel_responses],
        "normal_table_programs_first_order": not untouched_first_order,
        "processing_block_programmed_by_source": "PRB_R2",
        "register_reads_are_cache_only": ".read\t\t\t= aic31xx_read_reg_cache" in source,
        "higher_coefficient_pages_readable_through_cache": False,
        "diagnostic_mode_control": "IDC Enable",
        "diagnostic_mode_live_value": "must read separately; source default alone is not proof",
        "hardware_filter_control_exposed": False,
        "miniDSP_custom_program_loaded_by_tater": False,
        "hardware_writes": 0,
        "vendor_coefficients_exported": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("biscuit", "radar"), required=True)
    parser.add_argument("--source-root", type=Path,
                        help="private extracted 3.18_hl directory; summaries/hashes only")
    args = parser.parse_args()
    result = {"target": args.target, "candidate": candidate()}
    if args.source_root:
        codec = args.source_root / "sound/soc/codecs"
        try:
            result["source_audit"] = audit_source(
                (codec / "tlv320aic3101.c").read_bytes(),
                (codec / "tlv320aic3101.h").read_bytes(), args.target)
        except (OSError, ValueError) as error:
            parser.exit(2, f"refusing source audit: {error}\n")
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
