#!/usr/bin/env python3
"""Read-only Athena codec prerequisites; never load firmware or qualify codecs.

Run against / on the selected Linux device, or an owner-local root snapshot.
An optional SCP ELF is inspected as data, never executed, copied or modified.
Reports omit serial numbers, calibration, firmware strings and firmware bytes.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct


CONFIG_KEYS = (
    'CONFIG_VIDEO_DEV', 'CONFIG_V4L_MEM2MEM_DRIVERS',
    'CONFIG_VIDEO_MEDIATEK_VCODEC', 'CONFIG_MTK_SCP',
    'CONFIG_RPMSG_MTK_SCP', 'CONFIG_MEDIA_CONTROLLER', 'CONFIG_MTK_IOMMU',
)
SCP_SRAM_BYTES = 0x80000
SCP_DRAM_BYTES = 0x500000  # Linux 6.18.56 default_scp_sizes
SCP_MIN_POOL_BYTES = 0x800000  # coherent allocator rounds get_order(5MiB)
MAX_ELF_BYTES = 16 * 1024 * 1024


def read_bounded(path: Path, limit: int = 1024 * 1024) -> bytes:
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError('input exceeds inspection bound')
    return data


def scp_elf_contract(path: Path) -> dict:
    data = read_bounded(path, MAX_ELF_BYTES)
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('SCP requires ELF32 little-endian version 1')
    fields = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    kind, machine, version, _, phoff, shoff, _, ehsize, phsize, phnum, shsize, shnum, shstrndx = fields
    if kind != 2 or machine != 40 or version != 1 or ehsize != 52:
        raise ValueError('SCP input is not an ARM ELF32 executable')
    if not 0 < phnum <= 1024 or phsize != 32 or phoff < ehsize or phoff + phnum * phsize > len(data):
        raise ValueError('invalid program header table')
    segments = []
    for index in range(phnum):
        entry = struct.unpack_from('<8I', data, phoff + index * phsize)
        ptype, offset, vaddr, paddr, filesz, memsz, flags, _ = entry
        if ptype != 1:
            continue
        if filesz > memsz or offset + filesz > len(data) or paddr + memsz > 2**32:
            raise ValueError('invalid load segment')
        if not memsz:
            continue
        if paddr < SCP_SRAM_BYTES and paddr + memsz > SCP_SRAM_BYTES:
            raise ValueError('load segment crosses SRAM boundary')
        segments.append({'physical_address': paddr, 'virtual_address': vaddr,
                         'file_bytes': filesz, 'memory_bytes': memsz,
                         'storage': 'sram' if paddr < SCP_SRAM_BYTES else 'dram'})
    if not segments:
        raise ValueError('no load segments')
    ipi = None
    if shnum:
        if shnum > 4096 or shsize != 40 or shoff < ehsize or shoff + shnum * shsize > len(data) or shstrndx >= shnum:
            raise ValueError('invalid section table')
        string_header = struct.unpack_from('<10I', data, shoff + shstrndx * shsize)
        start, length = string_header[4:6]
        if start + length > len(data):
            raise ValueError('truncated section names')
        names = data[start:start+length]
        for index in range(shnum):
            section = struct.unpack_from('<10I', data, shoff + index * shsize)
            if section[0] >= len(names):
                raise ValueError('invalid section name')
            name_end = names.find(b'\0', section[0])
            if name_end < 0:
                raise ValueError('unterminated section name')
            if names[section[0]:name_end] == b'.ipi_buffer':
                if ipi is not None or section[3] + section[5] > SCP_SRAM_BYTES:
                    raise ValueError('invalid IPI buffer')
                ipi = {'address': section[3], 'bytes': section[5]}
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
            'load_segments': segments, 'ipi_buffer': ipi,
            'driver_dram_allocation_bytes': SCP_DRAM_BYTES,
            'coherent_pool_minimum_bytes': SCP_MIN_POOL_BYTES,
            'compatible_with_this_board': 'unproven',
            'safe_to_load': False}


def strings(path: Path) -> list[str]:
    return [part.decode('ascii') for part in read_bounded(path).split(b'\0') if part]


def enabled(node: Path) -> bool:
    status = node/'status'
    return not status.exists() or strings(status) in (['ok'], ['okay'])


def kernel_config(path: Path) -> dict[str, str]:
    data = read_bounded(path)
    if data.startswith(b'\x1f\x8b'):
        # Read bounded even for corrupted or untrusted compressed snapshots.
        import io
        with gzip.GzipFile(fileobj=io.BytesIO(data)) as stream:
            data = stream.read(2 * 1024 * 1024 + 1)
        if len(data) > 2 * 1024 * 1024:
            raise ValueError('expanded config exceeds inspection bound')
    config = {}
    for line in data.decode('ascii').splitlines():
        match = re.fullmatch(r'(CONFIG_[A-Z0-9_]+)=([ymn])', line)
        if match:
            config[match[1]] = match[2]
        match = re.fullmatch(r'# (CONFIG_[A-Z0-9_]+) is not set', line)
        if match:
            config[match[1]] = 'n'
    return {key: config.get(key, 'unknown') for key in CONFIG_KEYS}


def audit(root: Path, config_path: Path | None = None) -> dict:
    dt = root/'sys/firmware/devicetree/base'
    compatible = strings(dt/'compatible')
    if 'amzn,athena' not in compatible or 'mediatek,mt8183' not in compatible:
        raise ValueError('not an Athena MT8183 device tree')
    if not (root/'sys/bus/platform/devices').is_dir():
        raise ValueError('snapshot lacks platform driver observations')
    config = kernel_config(config_path or root/'proc/config.gz')
    nodes = {}
    for name, address in (('scp', '10500000'), ('encoder', '17020000'), ('decoder', '16020000')):
        matches = list((dt/'soc').glob('*@'+address))
        if len(matches) != 1:
            raise ValueError('missing or ambiguous '+name+' node')
        node = matches[0]
        platform = root/'sys/bus/platform/devices'/(address+'.'+node.name.split('@')[0])
        driver = platform/'driver'
        nodes[name] = {'dt_enabled': enabled(node),
                       'compatible': strings(node/'compatible'),
                       'driver_bound': driver.is_symlink(),
                       'driver': driver.readlink().name if driver.is_symlink() else None}
        if name == 'scp':
            nodes[name]['memory_region_present'] = (node/'memory-region').exists()
    blockers = []
    for key in CONFIG_KEYS:
        if config[key] not in ('y', 'm'):
            blockers.append('kernel configuration lacks '+key)
    if not nodes['scp']['dt_enabled']:
        blockers.append('SCP disabled in device tree')
    if not nodes['scp']['memory_region_present']:
        blockers.append('SCP has no declared reserved-memory region')
    for name, node in nodes.items():
        if not node['driver_bound']:
            blockers.append(name+' driver is not bound')
    return {'schema': 1, 'target': 'athena', 'kernel_config': config, 'nodes': nodes,
            'blockers': blockers, 'hardware_codec_qualified': False,
            'qualification_note': 'Prerequisites only; generated encode/decode and live privacy/A/V tests remain mandatory.'}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('/'))
    parser.add_argument('--config', type=Path)
    parser.add_argument('--scp-elf', type=Path)
    args = parser.parse_args()
    try:
        report = audit(args.root, args.config)
        if args.scp_elf:
            report['candidate_firmware'] = scp_elf_contract(args.scp_elf)
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        parser.exit(1, f'refusing inspection: {error}\n')
    print(json.dumps(report, sort_keys=True, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
