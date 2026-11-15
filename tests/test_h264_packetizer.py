import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VCODEC = ROOT / "probes" / "mt8163-vcodec"


class H264PacketizerTests(unittest.TestCase):
    def compile_packetizer(self, directory: Path) -> Path:
        executable = directory / "packetize"
        subprocess.run(
            [
                "cc", "-O2", "-Wall", "-Wextra", "-Werror",
                "-std=c11", str(VCODEC / "h264_packetize.c"),
                "-o", str(executable),
            ],
            check=True,
        )
        return executable

    def test_packetizer_preserves_prefix_and_access_units(self) -> None:
        # SPS/PPS-like prefix before the first AUD belongs to the first packet.
        prefix = b"\x00\x00\x00\x01\x67prefix"
        first = b"\x00\x00\x01\x09\xf0\x00\x00\x01\x65first"
        second = b"\x00\x00\x00\x01\x09\xf0\x00\x00\x01\x41second"
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            executable = self.compile_packetizer(directory)
            source = directory / "input.h264"
            output = directory / "output.tvp"
            source.write_bytes(prefix + first + second)
            result = subprocess.run(
                [str(executable), str(source), str(output)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertIn('"access_units":2', result.stdout)
            data = output.read_bytes()
            records = []
            offset = 0
            while True:
                (size,) = struct.unpack_from("<I", data, offset)
                offset += 4
                if size == 0:
                    break
                records.append(data[offset : offset + size])
                offset += size
            self.assertEqual(records, [prefix + first, second])
            self.assertEqual(offset, len(data))

            refused = subprocess.run(
                [str(executable), str(source), str(output)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(refused.returncode, 2)
            self.assertIn("refusing to overwrite", refused.stderr)

    def test_v2_packetizer_adds_microsecond_timestamps(self) -> None:
        first = b"\x00\x00\x01\x09\xf0\x00\x00\x01\x65first"
        second = b"\x00\x00\x01\x09\xf0\x00\x00\x01\x41second"
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            executable = self.compile_packetizer(directory)
            source = directory / "input.h264"
            output = directory / "output.tvp2"
            source.write_bytes(first + second)
            result = subprocess.run(
                [str(executable), "--v2", "25", str(source), str(output)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertIn('"protocol":2', result.stdout)
            data = output.read_bytes()
            offset = 0
            records = []
            while True:
                size, pts_us = struct.unpack_from("<Iq", data, offset)
                offset += 12
                if size == 0:
                    break
                records.append((pts_us, data[offset : offset + size]))
                offset += size
            self.assertEqual(records, [(0, first), (40_000, second)])
            self.assertEqual(offset, len(data))


if __name__ == "__main__":
    unittest.main()
