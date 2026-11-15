import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/verify_athena_codec_pattern.py'
spec = importlib.util.spec_from_file_location('athena_codec_pattern', SCRIPT)
pattern = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pattern)


def frame(index=0):
    data = bytearray()
    for plane in range(3):
        scale = 2 if plane else 1
        width, height = pattern.WIDTH // scale, pattern.HEIGHT // scale
        pixels = bytearray(bytes(sum(([v] * (width // 4) for v in pattern.COLORS[plane]), [])) * height)
        for row in range(32 // scale):
            for bit in range(6):
                value = 128 if plane else (235 if index & (1 << bit) else 16)
                start = row * width + bit * 32 // scale
                pixels[start:start + 32 // scale] = bytes([value]) * (32 // scale)
        for row in range(height - 64 // scale, height):
            start = row * width + index * 8 // scale
            pixels[start:start + 8 // scale] = bytes([128 if plane else 235]) * (8 // scale)
        data.extend(pixels)
    return bytes(data)


class AthenaCodecPatternTests(unittest.TestCase):
    def test_generated_pixels_and_id_pass(self):
        for index in (0, 1, 31, 63):
            self.assertEqual(pattern.validate_frame(frame(index), index)['frame_id'], index)

    def test_exit_zero_blank_output_is_not_valid_video(self):
        with self.assertRaisesRegex(ValueError, 'all-zero'):
            pattern.validate_frame(bytes(pattern.FRAME_BYTES), 0)

    def test_swapped_chroma_fails(self):
        data = frame()
        y, quarter = pattern.WIDTH * pattern.HEIGHT, pattern.WIDTH * pattern.HEIGHT // 4
        swapped = data[:y] + data[y + quarter:] + data[y:y + quarter]
        with self.assertRaisesRegex(ValueError, 'color mismatch'):
            pattern.validate_frame(swapped, 0)

    def test_repeated_and_reordered_frames_fail(self):
        for actual, expected in ((0, 1), (3, 2), (1, 5)):
            with self.subTest(actual=actual, expected=expected), self.assertRaisesRegex(ValueError, 'order mismatch'):
                pattern.validate_frame(frame(actual), expected)

    def test_marker_is_checked_independently(self):
        data = bytearray(frame(1))
        for row in range(pattern.HEIGHT - 32, pattern.HEIGHT - 24):
            data[row * pattern.WIDTH + 8:row * pattern.WIDTH + 16] = bytes([81]) * 8
        with self.assertRaisesRegex(ValueError, 'moving-marker'):
            pattern.validate_frame(bytes(data), 1)

    def test_bad_frame_bounds_fail(self):
        for data, index in ((b'', 0), (frame()[:-1], 0), (frame(), 64)):
            with self.assertRaises(ValueError):
                pattern.validate_frame(data, index)

    def test_validated_file_is_not_production_qualification(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'generated.yuv'
            path.write_bytes(frame() + frame(1))
            report = pattern.verify(path, 2)
            self.assertTrue(report['generated_pattern_valid'])
            self.assertFalse(report['hardware_codec_qualified'])
            self.assertEqual(path.read_bytes(), frame() + frame(1))

    def test_truncated_extra_and_unbounded_files_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'generated.yuv'
            path.write_bytes(frame())
            for count in (0, 2, 65):
                with self.assertRaises(ValueError):
                    pattern.verify(path, count)


if __name__ == '__main__':
    unittest.main()
