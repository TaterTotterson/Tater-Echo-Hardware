#!/usr/bin/env python3
"""Offline validation of the generated 640x480 I420 codec qualification pattern.

Never opens hardware. Only use with synthetic red/green/blue/grey bars, six
32px frame-ID cells at the top and an 8px/frame bottom marker. An exit-zero
decoder or correctly sized output is not sufficient evidence of valid video.
Passing this check alone never qualifies a production hardware codec.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import statistics

WIDTH, HEIGHT = 640, 480
FRAME_BYTES = WIDTH * HEIGHT * 3 // 2
COLORS = ((81, 145, 41, 126), (90, 54, 240, 128), (240, 34, 110, 128))


def validate_frame(frame: bytes, index: int, tolerance: int = 12) -> dict:
    if len(frame) != FRAME_BYTES or not 0 <= index < 64 or not 0 <= tolerance <= 20:
        raise ValueError('invalid generated frame size/index/tolerance')
    if not any(frame):
        raise ValueError('blank all-zero decoded frame')
    color_samples = []
    offset = 0
    for plane in range(3):
        width, height = (WIDTH // 2, HEIGHT // 2) if plane else (WIDTH, HEIGHT)
        values = []
        for bar, expected in enumerate(COLORS[plane]):
            x, y = (bar * 2 + 1) * width // 8, height // 2
            sample = [frame[offset + row * width + col]
                      for row in range(y - 4, y + 4) for col in range(x - 4, x + 4)]
            value = statistics.median(sample)
            if abs(value - expected) > tolerance:
                raise ValueError(f'generated color mismatch in plane {plane}, bar {bar}')
            values.append(value)
        color_samples.append(values)
        offset += width * height
    decoded_id = 0
    for bit in range(6):
        x, y = bit * 32 + 16, 16
        value = statistics.median(frame[row * WIDTH + col]
                                 for row in range(y - 4, y + 4) for col in range(x - 4, x + 4))
        expected = 235 if index & (1 << bit) else 16
        if abs(value - expected) > tolerance:
            raise ValueError(f'frame-ID/order mismatch at output frame {index}')
        decoded_id |= int(value > 128) << bit
    marker = index * 8 + 4
    value = statistics.median(frame[row * WIDTH + col]
                             for row in range(HEIGHT - 32, HEIGHT - 24)
                             for col in range(marker - 2, marker + 2))
    if abs(value - 235) > tolerance:
        raise ValueError(f'moving-marker mismatch at output frame {index}')
    return {'frame_id': decoded_id, 'color_plane_samples': color_samples}


def verify(path: Path, frames: int) -> dict:
    if not 1 <= frames <= 64:
        raise ValueError('expected generated frame count must be 1..64')
    if path.stat().st_size != frames * FRAME_BYTES:
        raise ValueError('raw output length does not match the expected generated frame count')
    digest = hashlib.sha256()
    first = None
    with path.open('rb') as stream:
        for index in range(frames):
            frame = stream.read(FRAME_BYTES)
            digest.update(frame)
            report = validate_frame(frame, index)
            if first is None:
                first = report
        if stream.read(1):
            raise ValueError('raw output grew during validation')
    return {'generated_pattern_valid': True, 'frames': frames,
            'width': WIDTH, 'height': HEIGHT, 'pixel_format': 'I420',
            'sha256': digest.hexdigest(), 'first_frame': first,
            'hardware_codec_qualified': False}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated-only', action='store_true', required=True)
    parser.add_argument('--frames', type=int, required=True)
    parser.add_argument('raw_i420', type=Path)
    args = parser.parse_args()
    try:
        report = verify(args.raw_i420, args.frames)
    except (OSError, ValueError) as error:
        print(json.dumps({'generated_pattern_valid': False, 'error': str(error),
                          'hardware_codec_qualified': False}))
        raise SystemExit(1)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
