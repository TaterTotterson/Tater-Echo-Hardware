import struct
import tempfile
import unittest
from pathlib import Path

from tater_re.artifacts import classify
from tater_re.elf import parse_elf


def elf32_header(machine: int = 40) -> bytes:
    ident = b"\x7fELF" + bytes([1, 1, 1, 0, 0]) + bytes(7)
    return ident + struct.pack(
        "<HHIIIIIHHHHHH",
        3,
        machine,
        1,
        0,
        0,
        0,
        0,
        52,
        32,
        0,
        40,
        0,
        0,
    )


def elf64_header(machine: int = 183) -> bytes:
    ident = b"\x7fELF" + bytes([2, 1, 1, 0, 0]) + bytes(7)
    return ident + struct.pack(
        "<HHIQQQIHHHHHH",
        2,
        machine,
        1,
        0,
        0,
        0,
        0,
        64,
        56,
        0,
        64,
        0,
        0,
    )


class ELFTests(unittest.TestCase):
    def test_parses_arm32_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "libtest.so"
            path.write_bytes(elf32_header())
            metadata = parse_elf(path)
        self.assertEqual(metadata["bits"], 32)
        self.assertEqual(metadata["machine"], "arm")
        self.assertEqual(metadata["type"], "shared")
        self.assertEqual(metadata["needed"], [])

    def test_parses_aarch64_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "program"
            path.write_bytes(elf64_header())
            metadata = parse_elf(path)
        self.assertEqual(metadata["bits"], 64)
        self.assertEqual(metadata["machine"], "aarch64")
        self.assertEqual(metadata["type"], "executable")

    def test_classifies_common_firmware_magic(self) -> None:
        cases = {
            "boot.img": (b"ANDROID!" + bytes(2048), "android-boot-image"),
            "sparse.img": (b"\x3a\xff\x26\xed" + bytes(2048), "android-sparse-image"),
            "board.dtb": (b"\xd0\x0d\xfe\xed" + bytes(2048), "device-tree-blob"),
        }
        with tempfile.TemporaryDirectory() as temporary:
            for name, (content, expected) in cases.items():
                path = Path(temporary) / name
                path.write_bytes(content)
                self.assertEqual(classify(path)["kind"], expected)


if __name__ == "__main__":
    unittest.main()

