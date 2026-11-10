import tempfile
import unittest
from pathlib import Path

from tater_re.binary import search_strings


class BinaryTests(unittest.TestCase):
    def test_finds_strings_with_offsets(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "sample.bin"
            path.write_bytes(b"\x00\x01hello /dev/mtk_jpeg\x00other")
            matches = search_strings(path, "MTK_JPEG")
            self.assertEqual(matches[0]["offset"], 2)
            self.assertIn("/dev/mtk_jpeg", matches[0]["value"])


if __name__ == "__main__":
    unittest.main()
