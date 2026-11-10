"""Artifact classification and identity."""

from __future__ import annotations

import hashlib
import struct
from pathlib import Path
from typing import Any

from .elf import ELFError, parse_elf


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def classify(path: Path) -> dict[str, Any]:
    with path.open("rb") as source:
        head = source.read(4096)
        source.seek(1024)
        ext_super = source.read(64)

    kind = "data"
    details: dict[str, Any] = {}
    if head.startswith(b"\x7fELF"):
        kind = "elf"
        try:
            details["elf"] = parse_elf(path)
        except ELFError as error:
            details["parse_error"] = str(error)
    elif head.startswith(b"ANDROID!"):
        kind = "android-boot-image"
    elif head[:4] == b"\x3a\xff\x26\xed":
        kind = "android-sparse-image"
    elif head[:4] == b"\xd0\x0d\xfe\xed":
        kind = "device-tree-blob"
    elif head.startswith(b"PK\x03\x04"):
        kind = "zip"
    elif head.startswith(b"\x1f\x8b"):
        kind = "gzip"
    elif head.startswith(b"\xfd7zXZ\x00"):
        kind = "xz"
    elif head.startswith(b"\x04\x22\x4d\x18"):
        kind = "lz4"
    elif head.startswith((b"070701", b"070702", b"070707")):
        kind = "cpio"
    elif head.startswith(b"hsqs"):
        kind = "squashfs"
    elif len(ext_super) >= 58 and struct.unpack_from("<H", ext_super, 56)[0] == 0xEF53:
        kind = "ext-filesystem-image"
    elif head[:4] in (
        b"\xfe\xed\xfa\xce",
        b"\xce\xfa\xed\xfe",
        b"\xfe\xed\xfa\xcf",
        b"\xcf\xfa\xed\xfe",
        b"\xca\xfe\xba\xbe",
    ):
        kind = "mach-o"

    return {"kind": kind, **details}

