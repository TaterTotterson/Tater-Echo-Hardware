"""Small, dependency-free ELF metadata reader.

This intentionally extracts identity and dynamic dependency information only.
Ghidra remains responsible for disassembly and decompilation.
"""

from __future__ import annotations

import mmap
import struct
from pathlib import Path
from typing import Any


ELF_MAGIC = b"\x7fELF"

MACHINES = {
    3: "x86",
    8: "mips",
    20: "powerpc",
    21: "powerpc64",
    40: "arm",
    62: "x86_64",
    183: "aarch64",
    243: "riscv",
}

ELF_TYPES = {
    0: "none",
    1: "relocatable",
    2: "executable",
    3: "shared",
    4: "core",
}


class ELFError(ValueError):
    """Raised when an ELF is truncated or internally inconsistent."""


def _cstring(data: mmap.mmap, offset: int, limit: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    end_limit = min(len(data), offset + max(limit, 0))
    end = data.find(b"\0", offset, end_limit)
    if end < 0:
        end = end_limit
    return data[offset:end].decode("utf-8", "replace")


def _align4(value: int) -> int:
    return (value + 3) & ~3


def parse_elf(path: Path) -> dict[str, Any]:
    """Return stable metadata for a 32- or 64-bit ELF file."""

    with path.open("rb") as source:
        if source.seek(0, 2) < 52:
            raise ELFError("ELF header is truncated")
        source.seek(0)
        with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
            if data[:4] != ELF_MAGIC:
                raise ELFError("not an ELF file")
            elf_class = data[4]
            data_encoding = data[5]
            if elf_class not in (1, 2):
                raise ELFError(f"unsupported ELF class {elf_class}")
            if data_encoding not in (1, 2):
                raise ELFError(f"unsupported ELF byte order {data_encoding}")

            endian = "<" if data_encoding == 1 else ">"
            bits = 32 if elf_class == 1 else 64
            minimum = 52 if bits == 32 else 64
            if len(data) < minimum:
                raise ELFError("ELF header is truncated")

            e_type, machine = struct.unpack_from(endian + "HH", data, 16)
            if bits == 32:
                phoff = struct.unpack_from(endian + "I", data, 28)[0]
                phentsize, phnum = struct.unpack_from(endian + "HH", data, 42)
                expected_ph_size = 32
            else:
                phoff = struct.unpack_from(endian + "Q", data, 32)[0]
                phentsize, phnum = struct.unpack_from(endian + "HH", data, 54)
                expected_ph_size = 56

            segments: list[dict[str, int]] = []
            if phnum and phentsize < expected_ph_size:
                raise ELFError("program header entry is too small")
            if phoff + phnum * phentsize > len(data):
                raise ELFError("program header table is truncated")

            for index in range(phnum):
                offset = phoff + index * phentsize
                if bits == 32:
                    values = struct.unpack_from(endian + "IIIIIIII", data, offset)
                    p_type, p_offset, vaddr, _paddr, filesz, memsz, flags, align = values
                else:
                    values = struct.unpack_from(endian + "IIQQQQQQ", data, offset)
                    p_type, flags, p_offset, vaddr, _paddr, filesz, memsz, align = values
                if p_offset + filesz > len(data):
                    raise ELFError(f"program segment {index} is truncated")
                segments.append(
                    {
                        "type": p_type,
                        "flags": flags,
                        "offset": p_offset,
                        "vaddr": vaddr,
                        "filesz": filesz,
                        "memsz": memsz,
                        "align": align,
                    }
                )

            def virtual_to_file(address: int) -> int | None:
                for segment in segments:
                    if segment["type"] != 1:  # PT_LOAD
                        continue
                    start = segment["vaddr"]
                    if start <= address < start + segment["filesz"]:
                        return segment["offset"] + address - start
                return None

            interpreter = ""
            build_id = ""
            dynamic: list[tuple[int, int]] = []
            dyn_entry_size = 8 if bits == 32 else 16
            dyn_format = endian + ("II" if bits == 32 else "QQ")

            for segment in segments:
                if segment["type"] == 3:  # PT_INTERP
                    interpreter = _cstring(data, segment["offset"], segment["filesz"])
                elif segment["type"] == 2:  # PT_DYNAMIC
                    count = segment["filesz"] // dyn_entry_size
                    for index in range(count):
                        tag, value = struct.unpack_from(
                            dyn_format, data, segment["offset"] + index * dyn_entry_size
                        )
                        if tag == 0:
                            break
                        dynamic.append((tag, value))
                elif segment["type"] == 4 and not build_id:  # PT_NOTE
                    cursor = segment["offset"]
                    end = cursor + segment["filesz"]
                    while cursor + 12 <= end:
                        namesz, descsz, note_type = struct.unpack_from(endian + "III", data, cursor)
                        cursor += 12
                        name_start = cursor
                        cursor += _align4(namesz)
                        desc_start = cursor
                        cursor += _align4(descsz)
                        if cursor > end:
                            break
                        name = bytes(data[name_start : name_start + namesz]).rstrip(b"\0")
                        if name == b"GNU" and note_type == 3:
                            build_id = bytes(data[desc_start : desc_start + descsz]).hex()
                            break

            string_address = next((value for tag, value in dynamic if tag == 5), None)
            string_size = next((value for tag, value in dynamic if tag == 10), 1 << 20)
            string_offset = virtual_to_file(string_address) if string_address is not None else None

            def dynamic_string(index: int) -> str:
                if string_offset is None or index >= string_size:
                    return ""
                return _cstring(data, string_offset + index, string_size - index)

            needed = sorted(
                {dynamic_string(value) for tag, value in dynamic if tag == 1 and dynamic_string(value)}
            )
            soname_index = next((value for tag, value in dynamic if tag == 14), None)

            return {
                "bits": bits,
                "endianness": "little" if data_encoding == 1 else "big",
                "machine": MACHINES.get(machine, f"machine-{machine}"),
                "machine_id": machine,
                "type": ELF_TYPES.get(e_type, f"type-{e_type}"),
                "interpreter": interpreter,
                "soname": dynamic_string(soname_index) if soname_index is not None else "",
                "needed": needed,
                "build_id": build_id,
            }

