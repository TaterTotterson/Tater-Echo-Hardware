#!/usr/bin/env python3
"""Prepare an explicitly requested, default-off kernel experiment offline.

Requires the pinned official Biscuit/Radar GPL codec sources. Does not build,
flash, upload coefficients, modify firmware repositories or contact hardware.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
from pathlib import Path
import shutil

from adc3101_filter_lab import DRIVER_SHA256, HEADER_SHA256


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError("unexpected driver anchor; no partial source patch accepted")
    return source.replace(old, new, 1)


def patched_source(source: str) -> str:
    source = replace_once(source, '#include "tlv320aic3101.h"',
                          '#include "tlv320aic3101.h"\n#include "tater_adc3101_hpf_trial.h"')
    source = replace_once(source, 'static const struct snd_kcontrol_new aic31xx_snd_controls[] = {',
                          'static const struct snd_kcontrol_new aic31xx_snd_controls[] = {\n'
                          '\t{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = "Tater ADC HPF Status",\n'
                          '\t  .access = SNDRV_CTL_ELEM_ACCESS_READ, .info = tater_hpf_info, .get = tater_hpf_get },')
    source = replace_once(source, 'static void aic31xx_apply_tuning(struct snd_soc_codec *codec)',
                          'static int aic31xx_apply_tuning(struct snd_soc_codec *codec)')
    source = replace_once(source, '\t\tsnd_soc_write(codec, ADC_PRB_SELECT(i), 0x2);\n}',
                          '\t\tsnd_soc_write(codec, ADC_PRB_SELECT(i), 0x2);\n'
                          '\treturn tater_hpf_apply(codec, IDC_enabled != 0);\n}')
    # Clear the verified marker before each reset, not only after programming.
    source = replace_once(source, '\t/* Backup gains and mode settings before reset */',
                          '\tatomic_set(&tater_hpf_verified, 0);\n'
                          '\t/* Backup gains and mode settings before reset */')
    source = replace_once(source, '\t/* Program DSP */\n\taic31xx_apply_tuning(codec);',
                          '\t/* Program DSP; a failed optional filter must not start capture. */\n'
                          '\ti = aic31xx_apply_tuning(codec);\n\tif (i)\n\t\treturn i;')
    return source


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("biscuit", "radar"), required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new private directory; never overwrite")
    parser.add_argument("--prepare-offline-trial", action="store_true", required=True)
    args = parser.parse_args()
    driver = args.source_root / "sound/soc/codecs/tlv320aic3101.c"
    header = driver.with_suffix(".h")
    raw_driver, raw_header = driver.read_bytes(), header.read_bytes()
    if (hashlib.sha256(raw_driver).hexdigest(), hashlib.sha256(raw_header).hexdigest()) != (DRIVER_SHA256, HEADER_SHA256):
        parser.error("unexpected official codec source hash")
    result = patched_source(raw_driver.decode("utf-8"))
    args.output.mkdir(parents=False, exist_ok=False)
    (args.output / driver.name).write_text(result)
    (args.output / header.name).write_bytes(raw_header)
    helper = Path(__file__).with_name("tater_adc3101_hpf_trial.h")
    shutil.copyfile(helper, args.output / helper.name)
    patch = "".join(difflib.unified_diff(
        raw_driver.decode("utf-8").splitlines(keepends=True), result.splitlines(keepends=True),
        fromfile="a/sound/soc/codecs/tlv320aic3101.c", tofile="b/sound/soc/codecs/tlv320aic3101.c"))
    patch += "".join(difflib.unified_diff(
        [], helper.read_text().splitlines(keepends=True), fromfile="/dev/null",
        tofile="b/sound/soc/codecs/tater_adc3101_hpf_trial.h"))
    (args.output / "0051-tater-adc3101-hpf-trial.patch").write_text(patch)
    print(f"Prepared default-off {args.target} experiment at {args.output}; not built or installed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
