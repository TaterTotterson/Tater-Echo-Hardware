#!/usr/bin/env python3
"""Repack an Android boot-v0 image while preserving its non-kernel payloads."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"ANDROID!"
HEADER_FIELDS = struct.Struct("<10I")
ID_OFFSET = 576
ID_SIZE = 32


@dataclass(frozen=True)
class LegacyBoot:
    header_page: bytes
    kernel: bytes
    ramdisk: bytes
    second: bytes
    device_tree: bytes
    page_size: int


def align(value: int, page_size: int) -> int:
    return (value + page_size - 1) // page_size * page_size


def parse(image: bytes) -> LegacyBoot:
    if len(image) < ID_OFFSET + ID_SIZE or image[:8] != MAGIC:
        raise ValueError("not an Android legacy boot image")
    fields = HEADER_FIELDS.unpack_from(image, 8)
    kernel_size, _, ramdisk_size, _, second_size, _, _, page_size, dt_size, _ = fields
    if page_size < 512 or page_size > 65536 or page_size & (page_size - 1):
        raise ValueError(f"invalid legacy page size: {page_size}")
    if len(image) < page_size:
        raise ValueError("truncated header page")

    kernel_offset = page_size
    ramdisk_offset = kernel_offset + align(kernel_size, page_size)
    second_offset = ramdisk_offset + align(ramdisk_size, page_size)
    dt_offset = second_offset + align(second_size, page_size)
    required = dt_offset + align(dt_size, page_size)
    if required > len(image):
        raise ValueError("truncated legacy boot payload")
    return LegacyBoot(
        header_page=image[:page_size],
        kernel=image[kernel_offset : kernel_offset + kernel_size],
        ramdisk=image[ramdisk_offset : ramdisk_offset + ramdisk_size],
        second=image[second_offset : second_offset + second_size],
        device_tree=image[dt_offset : dt_offset + dt_size],
        page_size=page_size,
    )


def boot_id(boot: LegacyBoot) -> bytes:
    digest = hashlib.sha1()
    for component in (boot.kernel, boot.ramdisk, boot.second, boot.device_tree):
        digest.update(component)
        digest.update(struct.pack("<I", len(component)))
    return digest.digest()


def verify(image: bytes) -> LegacyBoot:
    boot = parse(image)
    if image[ID_OFFSET : ID_OFFSET + 20] != boot_id(boot):
        raise ValueError("legacy boot ID does not match its payloads")
    return boot


def padded(component: bytes, page_size: int) -> bytes:
    return component + bytes(align(len(component), page_size) - len(component))


def repack(donor: LegacyBoot, kernel: bytes, cmdline_append: str | None = None) -> bytes:
    if not kernel:
        raise ValueError("replacement kernel is empty")
    header = bytearray(donor.header_page)
    if cmdline_append is not None:
        extra = cmdline_append.encode("ascii")
        if not extra or any(byte < 32 or byte > 126 for byte in extra):
            raise ValueError("command-line addition must be nonempty printable ASCII")
        # Keep this narrow interface to the original 512-byte command field.
        # Refuse a donor using the later extended field rather than reordering it.
        if any(header[608:1632]):
            raise ValueError("extended command line is not supported for append")
        existing = bytes(header[64:576]).split(b"\0", 1)[0]
        command = existing + (b" " if existing else b"") + extra
        if len(command) > 511:
            raise ValueError("command line exceeds the legacy 511-byte limit")
        header[64:576] = command + bytes(512 - len(command))
    struct.pack_into("<I", header, 8, len(kernel))
    candidate = LegacyBoot(
        header_page=bytes(header),
        kernel=kernel,
        ramdisk=donor.ramdisk,
        second=donor.second,
        device_tree=donor.device_tree,
        page_size=donor.page_size,
    )
    header[ID_OFFSET : ID_OFFSET + ID_SIZE] = boot_id(candidate) + bytes(12)
    return b"".join(
        (
            bytes(header),
            padded(kernel, donor.page_size),
            padded(donor.ramdisk, donor.page_size),
            padded(donor.second, donor.page_size),
            padded(donor.device_tree, donor.page_size),
        )
    )


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("donor", type=Path)
    parser.add_argument("kernel", type=Path, nargs="?")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--cmdline-append", help="append printable ASCII to a short legacy command line")
    args = parser.parse_args()

    donor_data = args.donor.read_bytes()
    donor = verify(donor_data)
    if args.verify_only:
        if args.kernel is not None or args.output is not None or args.cmdline_append is not None:
            parser.error("--verify-only accepts only the donor image")
        print(
            json.dumps(
                {
                    "bytes": len(donor_data),
                    "kernel_bytes": len(donor.kernel),
                    "ramdisk_bytes": len(donor.ramdisk),
                    "page_size": donor.page_size,
                    "sha256": sha256(donor_data),
                    "verified": True,
                },
                sort_keys=True,
            )
        )
        return 0
    if args.kernel is None or args.output is None:
        parser.error("repack mode requires DONOR KERNEL OUTPUT")
    if args.output.exists():
        raise SystemExit(f"refusing to overwrite existing output: {args.output}")

    kernel = args.kernel.read_bytes()
    output = repack(donor, kernel, args.cmdline_append)
    verify(output)
    descriptor = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as file:
            file.write(output)
    except BaseException:
        args.output.unlink(missing_ok=True)
        raise
    print(
        json.dumps(
            {
                "bytes": len(output),
                "kernel_bytes": len(kernel),
                "ramdisk_preserved": donor.ramdisk == verify(output).ramdisk,
                "sha256": sha256(output),
                "verified": True,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
