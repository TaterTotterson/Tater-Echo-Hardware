import struct
import tempfile
import unittest
import uuid
from pathlib import Path
from unittest import mock

from tater_re.images import android_boot_metadata, ext4_list, gpt_metadata


class ImageTests(unittest.TestCase):
    def test_parses_legacy_android_boot_header(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "boot.img"
            header = bytearray(2048)
            header[:8] = b"ANDROID!"
            struct.pack_into(
                "<10I",
                header,
                8,
                1234,
                0x40008000,
                456,
                0x44000000,
                0,
                0,
                0x40000100,
                2048,
                0,
                0,
            )
            header[48:52] = b"test"
            header[64:75] = b"console=tty0"
            path.write_bytes(header + bytes(4096))
            metadata = android_boot_metadata(path)
            self.assertEqual(metadata["name"], "test")
            self.assertEqual(metadata["page_size"], 2048)
            self.assertEqual(metadata["kernel"]["offset"], 2048)
            self.assertEqual(metadata["ramdisk"]["offset"], 4096)

    def test_parses_gpt_partition_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "gpt.img"
            image = bytearray(34 * 512)
            disk_guid = uuid.uuid4()
            type_guid = uuid.uuid4()
            part_guid = uuid.uuid4()
            struct.pack_into(
                "<8sIIIIQQQQ16sQIII",
                image,
                512,
                b"EFI PART",
                0x00010000,
                92,
                0,
                0,
                1,
                99,
                34,
                90,
                disk_guid.bytes_le,
                2,
                4,
                128,
                0,
            )
            entry = 2 * 512
            image[entry : entry + 16] = type_guid.bytes_le
            image[entry + 16 : entry + 32] = part_guid.bytes_le
            struct.pack_into("<QQQ", image, entry + 32, 40, 49, 0)
            image[entry + 56 : entry + 56 + 12] = "system".encode("utf-16-le")
            path.write_bytes(image)
            metadata = gpt_metadata(path)
            self.assertEqual(metadata["disk_guid"], str(disk_guid))
            self.assertEqual(metadata["partitions"][0]["name"], "system")
            self.assertEqual(metadata["partitions"][0]["sectors"], 10)

    @mock.patch("tater_re.images._ext4_command")
    def test_parses_debugfs_machine_listing(self, command: mock.Mock) -> None:
        command.return_value = (
            "/12/040755/0/0/lib//\n"
            "/13/100644/0/0/libcodec.so/1234/\n"
            "/14/120644/0/0/link/8/\n"
        )
        entries = ext4_list(Path("system.img"))
        self.assertEqual([entry["kind"] for entry in entries], ["directory", "file", "symlink"])
        self.assertEqual(entries[1]["path"], "/libcodec.so")


if __name__ == "__main__":
    unittest.main()
