"""Read-only Android and Linux firmware image helpers."""

from __future__ import annotations

import fnmatch
import gzip
import hashlib
import json
import lzma
import os
import posixpath
import re
import shutil
import struct
import subprocess
import uuid
import zlib
from collections import deque
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from .workspace import WorkspaceError, is_sensitive_name


ANDROID_MAGIC = b"ANDROID!"
GPT_MAGIC = b"EFI PART"


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def _cstring(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("utf-8", "replace")


def android_boot_metadata(path: Path) -> dict[str, Any]:
    """Parse Android boot image headers v0-v4 without modifying the image."""

    with path.open("rb") as source:
        header = source.read(4096)
    if not header.startswith(ANDROID_MAGIC):
        raise WorkspaceError(f"not an Android boot image: {path}")
    if len(header) < 64:
        raise WorkspaceError("Android boot image header is truncated")

    # v3/v4 removed addresses and the configurable page size. The field at
    # legacy page-size offset 36 is reserved and therefore zero.
    legacy_page_size = struct.unpack_from("<I", header, 36)[0]
    if legacy_page_size == 0:
        kernel_size, ramdisk_size, os_version, header_size = struct.unpack_from(
            "<4I", header, 8
        )
        header_version = struct.unpack_from("<I", header, 40)[0]
        if header_version not in (3, 4):
            raise WorkspaceError(f"unsupported Android boot header version {header_version}")
        page_size = 4096
        cmdline = _cstring(header[44:1580])
        kernel_offset = page_size
        ramdisk_offset = kernel_offset + _align(kernel_size, page_size)
        result = {
            "header_version": header_version,
            "header_size": header_size,
            "page_size": page_size,
            "os_version_raw": os_version,
            "name": "",
            "cmdline": cmdline,
            "kernel": {"size": kernel_size, "offset": kernel_offset},
            "ramdisk": {"size": ramdisk_size, "offset": ramdisk_offset},
            "second": {"size": 0, "offset": 0},
        }
        if header_version == 4 and len(header) >= 1584:
            result["signature_size"] = struct.unpack_from("<I", header, 1580)[0]
        return result

    if legacy_page_size < 512 or legacy_page_size > 65536 or legacy_page_size & (
        legacy_page_size - 1
    ):
        raise WorkspaceError(f"invalid Android boot page size {legacy_page_size}")

    (
        kernel_size,
        kernel_address,
        ramdisk_size,
        ramdisk_address,
        second_size,
        second_address,
        tags_address,
        page_size,
        version_or_dt_size,
        os_version,
    ) = struct.unpack_from("<10I", header, 8)
    name = _cstring(header[48:64])
    cmdline = _cstring(header[64:576]) + _cstring(header[608:1632])
    kernel_offset = page_size
    ramdisk_offset = _align(kernel_offset + kernel_size, page_size)
    second_offset = _align(ramdisk_offset + ramdisk_size, page_size)
    return {
        "header_version": version_or_dt_size if version_or_dt_size <= 4 else 0,
        "legacy_dt_size": version_or_dt_size if version_or_dt_size > 4 else 0,
        "page_size": page_size,
        "os_version_raw": os_version,
        "name": name,
        "cmdline": cmdline,
        "kernel": {
            "size": kernel_size,
            "offset": kernel_offset,
            "address": kernel_address,
        },
        "ramdisk": {
            "size": ramdisk_size,
            "offset": ramdisk_offset,
            "address": ramdisk_address,
        },
        "second": {
            "size": second_size,
            "offset": second_offset,
            "address": second_address,
        },
        "tags_address": tags_address,
    }


def compression_kind(data: bytes) -> str:
    if data.startswith(b"\x1f\x8b"):
        return "gzip"
    if data.startswith(b"\xfd7zXZ\x00"):
        return "xz"
    if data.startswith(b"\x04\x22\x4d\x18"):
        return "lz4"
    if data.startswith((b"070701", b"070702", b"070707")):
        return "cpio"
    return "data"


def _safe_archive_members(names: Iterable[str]) -> list[str]:
    members = []
    for name in names:
        pure = PurePosixPath(name)
        if pure.is_absolute() or ".." in pure.parts:
            raise WorkspaceError(f"unsafe archive member: {name!r}")
        members.append(name)
    return members


def _unpack_ramdisk(payload: bytes, output: Path) -> dict[str, Any]:
    kind = compression_kind(payload)
    if kind == "gzip":
        cpio = gzip.decompress(payload)
    elif kind == "xz":
        cpio = lzma.decompress(payload)
    elif kind == "cpio":
        cpio = payload
    elif kind == "lz4":
        executable = shutil.which("lz4")
        if not executable:
            raise WorkspaceError("lz4 is required to decompress this ramdisk")
        result = subprocess.run(
            [executable, "-d", "-c"], input=payload, capture_output=True, check=False
        )
        if result.returncode:
            raise WorkspaceError(result.stderr.decode("utf-8", "replace").strip())
        cpio = result.stdout
    else:
        return {"compression": kind, "extracted": False}

    cpio_path = output / "ramdisk.cpio"
    cpio_path.write_bytes(cpio)
    bsdtar = shutil.which("bsdtar") or shutil.which("tar")
    if not bsdtar:
        return {"compression": kind, "extracted": False, "cpio": str(cpio_path)}
    listing = subprocess.run(
        [bsdtar, "-tf", str(cpio_path)], capture_output=True, text=True, check=False
    )
    if listing.returncode:
        raise WorkspaceError(f"could not list ramdisk cpio: {listing.stderr.strip()}")
    members = _safe_archive_members(listing.stdout.splitlines())
    rootfs = output / "ramdisk-root"
    rootfs.mkdir(exist_ok=True)
    extraction = subprocess.run(
        [bsdtar, "-xf", str(cpio_path), "-C", str(rootfs)],
        capture_output=True,
        text=True,
        check=False,
    )
    if extraction.returncode:
        raise WorkspaceError(f"could not extract ramdisk: {extraction.stderr.strip()}")
    return {
        "compression": kind,
        "extracted": True,
        "cpio": str(cpio_path),
        "root": str(rootfs),
        "members": len(members),
    }


def _extract_dtb_sequence(payload: bytes, output: Path) -> list[dict[str, Any]]:
    records = []
    cursor = 0
    while cursor + 8 <= len(payload):
        marker = payload.find(b"\xd0\x0d\xfe\xed", cursor)
        if marker < 0 or marker + 8 > len(payload):
            break
        size = struct.unpack_from(">I", payload, marker + 4)[0]
        if size < 40 or marker + size > len(payload):
            cursor = marker + 4
            continue
        content = payload[marker : marker + size]
        destination = output / f"kernel-dtb-{len(records)}.dtb"
        destination.write_bytes(content)
        record = {
            "path": str(destination),
            "offset": marker,
            "size": size,
            "sha256": hashlib.sha256(content).hexdigest(),
        }
        dtc = shutil.which("dtc")
        if dtc:
            source = output / f"kernel-dtb-{len(records)}.dts"
            result = subprocess.run(
                [dtc, "-q", "-I", "dtb", "-O", "dts", "-o", str(source), str(destination)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode == 0:
                record["source"] = str(source)
        records.append(record)
        cursor = marker + size
    return records


def _unpack_kernel(payload: bytes, output: Path) -> dict[str, Any]:
    result: dict[str, Any] = {"format": compression_kind(payload)}
    kernel_payload = payload
    if len(payload) >= 512 and struct.unpack_from("<I", payload)[0] == 0x58881688:
        declared_size = struct.unpack_from("<I", payload, 4)[0]
        result["mediatek_header"] = {
            "name": _cstring(payload[8:40]),
            "declared_size": declared_size,
            "header_size": 512,
        }
        kernel_payload = payload[512 : 512 + declared_size]
        result["format"] = compression_kind(kernel_payload)

    if result["format"] == "gzip":
        decompressor = zlib.decompressobj(16 + zlib.MAX_WBITS)
        unpacked = decompressor.decompress(kernel_payload) + decompressor.flush()
        destination = output / "kernel-uncompressed.bin"
        destination.write_bytes(unpacked)
        result["uncompressed"] = {
            "path": str(destination),
            "size": len(unpacked),
            "sha256": hashlib.sha256(unpacked).hexdigest(),
        }
        tail = decompressor.unused_data
        if tail:
            result["appended_dtbs"] = _extract_dtb_sequence(tail, output)

        start = unpacked.find(b"IKCFG_ST")
        end = unpacked.find(b"IKCFG_ED", start + 8) if start >= 0 else -1
        if start >= 0 and end > start + 10:
            compressed_config = unpacked[start + 8 : end]
            try:
                config = gzip.decompress(compressed_config)
            except (OSError, EOFError):
                config = b""
            if config:
                config_path = output / "kernel.config"
                config_path.write_bytes(config)
                result["config"] = str(config_path)
    return result


def extract_android_boot(path: Path, output: Path) -> dict[str, Any]:
    metadata = android_boot_metadata(path)
    output.mkdir(parents=True, exist_ok=True)
    results: dict[str, Any] = {"metadata": metadata, "output": str(output)}
    with path.open("rb") as source:
        for name in ("kernel", "ramdisk", "second"):
            segment = metadata[name]
            size = int(segment["size"])
            if not size:
                continue
            source.seek(int(segment["offset"]))
            payload = source.read(size)
            if len(payload) != size:
                raise WorkspaceError(f"Android boot {name} segment is truncated")
            destination = output / f"{name}.bin"
            destination.write_bytes(payload)
            results[name] = {
                "path": str(destination),
                "size": size,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "format": compression_kind(payload),
            }
            if name == "kernel":
                results["kernel"].update(_unpack_kernel(payload, output))
            elif name == "ramdisk":
                results["ramdisk"].update(_unpack_ramdisk(payload, output))
    (output / "boot.json").write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return results


def gpt_metadata(path: Path, sector_size: int = 512) -> dict[str, Any]:
    with path.open("rb") as source:
        source.seek(sector_size)
        header = source.read(92)
        if len(header) < 92 or not header.startswith(GPT_MAGIC):
            raise WorkspaceError(f"not a GPT image: {path}")
        (
            _signature,
            revision,
            header_size,
            header_crc,
            _reserved,
            current_lba,
            backup_lba,
            first_usable,
            last_usable,
            disk_guid_raw,
            entries_lba,
            entry_count,
            entry_size,
            entries_crc,
        ) = struct.unpack("<8sIIIIQQQQ16sQIII", header)
        if entry_size < 128 or entry_size > 4096 or entry_count > 16384:
            raise WorkspaceError("GPT entry table dimensions are invalid")
        source.seek(entries_lba * sector_size)
        table = source.read(entry_count * entry_size)

    partitions = []
    for index in range(entry_count):
        entry = table[index * entry_size : (index + 1) * entry_size]
        if len(entry) < 128 or not any(entry[:16]):
            continue
        type_guid = str(uuid.UUID(bytes_le=entry[:16]))
        unique_guid = str(uuid.UUID(bytes_le=entry[16:32]))
        first_lba, last_lba, attributes = struct.unpack_from("<QQQ", entry, 32)
        name = entry[56:128].decode("utf-16-le", "replace").rstrip("\0")
        partitions.append(
            {
                "index": index + 1,
                "name": name,
                "type_guid": type_guid,
                "unique_guid": unique_guid,
                "first_lba": first_lba,
                "last_lba": last_lba,
                "sectors": last_lba - first_lba + 1,
                "bytes": (last_lba - first_lba + 1) * sector_size,
                "attributes": attributes,
            }
        )
    return {
        "sector_size": sector_size,
        "revision": revision,
        "header_size": header_size,
        "header_crc32": header_crc,
        "entries_crc32": entries_crc,
        "current_lba": current_lba,
        "backup_lba": backup_lba,
        "first_usable_lba": first_usable,
        "last_usable_lba": last_usable,
        "disk_guid": str(uuid.UUID(bytes_le=disk_guid_raw)),
        "partitions": partitions,
    }


def find_debugfs() -> Path | None:
    configured = os.environ.get("DEBUGFS")
    candidates = [
        configured,
        shutil.which("debugfs"),
        "/opt/homebrew/opt/e2fsprogs/sbin/debugfs",
        "/usr/local/opt/e2fsprogs/sbin/debugfs",
    ]
    for value in candidates:
        if value and Path(value).is_file() and os.access(value, os.X_OK):
            return Path(value).resolve()
    return None


_EXT4_LINE = re.compile(
    r"^/(?P<inode>\d+)/(?P<mode>[0-7]+)/(?P<uid>\d+)/(?P<gid>\d+)/"
    r"(?P<name>.*?)/(?P<size>\d*)/$"
)


def _ext4_command(image: Path, command: str) -> str:
    executable = find_debugfs()
    if not executable:
        raise WorkspaceError("debugfs was not found; install e2fsprogs or set DEBUGFS")
    result = subprocess.run(
        [str(executable), "-R", command, str(image)],
        capture_output=True,
        text=True,
        check=False,
        timeout=120,
    )
    if result.returncode:
        raise WorkspaceError(f"debugfs failed: {result.stderr.strip()}")
    if "File not found" in result.stderr or "not found" in result.stderr.lower():
        raise WorkspaceError(result.stderr.strip())
    return result.stdout


def _ext4_path(value: str) -> str:
    if any(character in value for character in ('\n', '\r', '"', "\\")):
        raise WorkspaceError("unsupported control character in ext4 path")
    normalized = posixpath.normpath("/" + value.lstrip("/"))
    if normalized == "/.." or normalized.startswith("/../"):
        raise WorkspaceError(f"unsafe ext4 path: {value!r}")
    return normalized


def ext4_list(path: Path, directory: str = "/") -> list[dict[str, Any]]:
    directory = _ext4_path(directory)
    output = _ext4_command(path, f'ls -p "{directory}"')
    entries = []
    for line in output.splitlines():
        match = _EXT4_LINE.match(line)
        if not match:
            continue
        values = match.groupdict()
        name = values["name"]
        if name in (".", ".."):
            continue
        mode = values["mode"]
        kind_bits = mode[:2]
        kind = {"04": "directory", "10": "file", "12": "symlink"}.get(
            kind_bits, "other"
        )
        child = posixpath.join(directory.rstrip("/") or "/", name)
        entries.append(
            {
                "path": child,
                "name": name,
                "kind": kind,
                "inode": int(values["inode"]),
                "mode": mode,
                "uid": int(values["uid"]),
                "gid": int(values["gid"]),
                "size": int(values["size"] or 0),
            }
        )
    return entries


def ext4_walk(path: Path, root: str = "/") -> list[dict[str, Any]]:
    root = _ext4_path(root)
    queue = deque([root])
    visited = set()
    entries = []
    while queue:
        directory = queue.popleft()
        if directory in visited:
            continue
        visited.add(directory)
        children = ext4_list(path, directory)
        entries.extend(children)
        queue.extend(child["path"] for child in children if child["kind"] == "directory")
    return entries


def extract_ext4(
    image: Path, patterns: list[str], output: Path, root: str = "/"
) -> list[dict[str, Any]]:
    entries = ext4_walk(image, root)
    normalized_patterns = [_ext4_path(pattern) for pattern in patterns]
    matches = [
        entry
        for entry in entries
        if entry["kind"] == "file"
        and not is_sensitive_name(entry["name"])
        and any(fnmatch.fnmatchcase(entry["path"], pattern) for pattern in normalized_patterns)
    ]
    if not matches:
        raise WorkspaceError("no regular files matched the requested ext4 paths")
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    extracted = []
    for entry in matches:
        destination = (output / entry["path"].lstrip("/")).resolve()
        if output not in destination.parents:
            raise WorkspaceError(f"unsafe extraction destination: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        _ext4_command(image, f'dump "{entry["path"]}" "{destination}"')
        result = dict(entry)
        result["destination"] = str(destination)
        extracted.append(result)
    return extracted
