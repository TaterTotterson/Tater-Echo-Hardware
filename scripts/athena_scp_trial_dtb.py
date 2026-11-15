#!/usr/bin/env python3
"""Audit an Athena research DT overlay without dumping private DT properties.

Offline only. This validates the narrowly scoped reservation/node change,
not firmware compatibility or hardware qualification. No device operations.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

POOL = '/reserved-memory/scp-codec-pool@50000000'
SCP = '/soc/scp@10500000'
BASE = 0x50000000
# kernel/dma/coherent.c uses get_order/bitmap_find_free_region: a 5MiB
# dma_alloc_coherent request needs an 8MiB power-of-two region in this pool.
SIZE = 0x800000


def properties(data: bytes) -> dict[tuple[str, str], bytes]:
    if not 40 <= len(data) <= 2 * 1024 * 1024:
        raise ValueError('DTB size outside inspection bounds')
    magic, total, off, strings_at, _, version, _, _, strings_size, struct_size = struct.unpack_from('>10I', data)
    if magic != 0xd00dfeed or total != len(data) or version != 17:
        raise ValueError('not a bounded v17 DTB')
    end = off + struct_size
    if off < 40 or end > len(data) or strings_at + strings_size > len(data):
        raise ValueError('DTB sections outside input')
    strings = data[strings_at:strings_at+strings_size]
    stack: list[str] = []
    result = {}
    while off + 4 <= end:
        token = struct.unpack_from('>I', data, off)[0]
        off += 4
        if token == 1:
            name_end = data.find(b'\0', off, end)
            if name_end < 0:
                raise ValueError('unterminated node')
            name = data[off:name_end].decode('ascii')
            if '/' in name or (not stack and name):
                raise ValueError('invalid node path')
            stack.append(name)
            off = (name_end + 4) & ~3
        elif token == 2:
            if not stack:
                raise ValueError('unbalanced node')
            stack.pop()
        elif token == 3:
            if off + 8 > end or not stack:
                raise ValueError('truncated property')
            length, name_at = struct.unpack_from('>II', data, off)
            off += 8
            if off + length > end or name_at >= len(strings):
                raise ValueError('property outside bounds')
            name_end = strings.find(b'\0', name_at)
            if name_end < 0:
                raise ValueError('unterminated property name')
            name = strings[name_at:name_end].decode('ascii')
            key = ('/' + '/'.join(stack[1:]), name)
            if key in result:
                raise ValueError('duplicate property')
            result[key] = data[off:off+length]
            off = (off + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            if stack:
                raise ValueError('unbalanced tree')
            return result
        else:
            raise ValueError('unknown structure token')
    raise ValueError('missing tree end')


def audit(before: dict, after: dict) -> dict:
    compatible = before.get(('/', 'compatible'), b'').split(b'\0')
    if b'amzn,athena' not in compatible or b'mediatek,mt8183' not in compatible:
        raise ValueError('not the exact Athena SoC family')
    if before.get((SCP, 'status')) != b'disabled\0' or (SCP, 'memory-region') in before:
        raise ValueError('expected original disabled, unreserved SCP')
    if before.get(('/reserved-memory', '#address-cells')) != struct.pack('>I', 2) or before.get(('/reserved-memory', '#size-cells')) != struct.pack('>I', 2):
        raise ValueError('unexpected reserved-memory cells')
    phandle = after.get((POOL, 'phandle'))
    if phandle is None or len(phandle) != 4 or phandle in (bytes(4), b'\xff'*4):
        raise ValueError('missing valid pool phandle')
    expected = {
        (POOL, 'compatible'): b'shared-dma-pool\0',
        (POOL, 'reg'): struct.pack('>4I', 0, BASE, 0, SIZE),
        (POOL, 'no-map'): b'',
        (POOL, 'phandle'): phandle,
        (SCP, 'memory-region'): phandle,
        (SCP, 'firmware-name'): b'tater-athena-scp.img\0',
        (SCP, 'status'): b'okay\0',
        ('/__symbols__', 'tater_scp_trial_pool'): POOL.encode() + b'\0',
    }
    # Existing phandles must stay unique and unchanged. All old properties,
    # including speaker calibration, must remain byte-for-byte intact.
    for key, value in before.items():
        if key not in expected and after.get(key) != value:
            raise ValueError('unexpected existing-property change: ' + str(key))
    for key, value in after.items():
        if key in expected:
            if value != expected[key]:
                raise ValueError('unexpected trial property: ' + str(key))
        elif key not in before:
            raise ValueError('unexpected new property: ' + str(key))
    if any(after.get(key) != value for key, value in expected.items()):
        raise ValueError('incomplete overlay')
    if any(value == phandle for (path, prop), value in before.items() if prop == 'phandle'):
        raise ValueError('pool phandle collides')
    for (path, name), value in before.items():
        if path.startswith('/reserved-memory/') and name == 'reg':
            if len(value) % 16:
                raise ValueError('unexpected reservation format')
            for start in range(0, len(value), 16):
                address, length = struct.unpack_from('>QQ', value, start)
                if length and address < BASE + SIZE and BASE < address + length:
                    raise ValueError('overlapping existing reservation')
    return {'scope_valid': True, 'existing_properties_preserved': True,
            'pool_address': BASE, 'pool_bytes': SIZE,
            'firmware_compatible': 'unproven', 'hardware_qualified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('trial', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(audit(properties(args.original.read_bytes()), properties(args.trial.read_bytes())), indent=2))
    except (ValueError, OSError, UnicodeError, struct.error) as error:
        parser.exit(1, 'refusing: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
