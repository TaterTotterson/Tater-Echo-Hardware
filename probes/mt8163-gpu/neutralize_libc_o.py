#!/usr/bin/env python3
"""Make four Android O imports available to a preloaded compatibility DSO."""

from __future__ import annotations

import pathlib
import struct
import sys


TARGETS = {
    "__system_property_read_callback",
    "__system_property_wait",
    "strtod_l",
    "strtof_l",
}


def c_string(data: bytes | bytearray, offset: int) -> str:
    end = data.index(0, offset)
    return bytes(data[offset:end]).decode("ascii")


def patch(path: pathlib.Path) -> None:
    data = bytearray(path.read_bytes())
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError(f"{path}: expected ELF32 little-endian")

    section_offset = struct.unpack_from("<I", data, 0x20)[0]
    section_size, section_count, names_index = struct.unpack_from(
        "<HHH", data, 0x2E
    )
    sections = []
    for index in range(section_count):
        offset = section_offset + index * section_size
        fields = struct.unpack_from("<10I", data, offset)
        sections.append(
            {
                "name_index": fields[0],
                "offset": fields[4],
                "size": fields[5],
                "link": fields[6],
                "entry_size": fields[9],
            }
        )

    names = sections[names_index]
    names_data = data[names["offset"] : names["offset"] + names["size"]]
    by_name = {
        c_string(names_data, section["name_index"]): section
        for section in sections
    }
    dynsym = by_name[".dynsym"]
    dynstr = sections[dynsym["link"]]
    versym = by_name[".gnu.version"]
    dynstr_data = data[dynstr["offset"] : dynstr["offset"] + dynstr["size"]]
    changed = []

    symbol_count = dynsym["size"] // dynsym["entry_size"]
    for index in range(symbol_count):
        symbol_offset = dynsym["offset"] + index * dynsym["entry_size"]
        name_offset = struct.unpack_from("<I", data, symbol_offset)[0]
        name = c_string(dynstr_data, name_offset)
        if name not in TARGETS:
            continue
        version_offset = versym["offset"] + index * 2
        version = struct.unpack_from("<H", data, version_offset)[0]
        if version != 4:
            raise ValueError(
                f"{path}: {name} has unexpected version index {version}"
            )
        struct.pack_into("<H", data, version_offset, 1)
        changed.append(name)

    if changed:
        path.write_bytes(data)
        print(f"{path.name}: neutralized {', '.join(sorted(changed))}")


if len(sys.argv) < 2:
    raise SystemExit(f"usage: {sys.argv[0]} ELF [ELF ...]")
for argument in sys.argv[1:]:
    patch(pathlib.Path(argument))
