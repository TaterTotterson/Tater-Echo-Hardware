import importlib.util
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "repack_legacy_boot.py"
SPEC = importlib.util.spec_from_file_location("repack_legacy_boot", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
BOOT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BOOT
SPEC.loader.exec_module(BOOT)


def synthetic_boot(kernel=b"old-kernel", ramdisk=b"ramdisk") -> bytes:
    page_size = 2048
    header = bytearray(page_size)
    header[:8] = BOOT.MAGIC
    struct.pack_into(
        "<10I",
        header,
        8,
        len(kernel),
        0x40080000,
        len(ramdisk),
        0x45F00000,
        0,
        0,
        0x48000000,
        page_size,
        0,
        0,
    )
    provisional = BOOT.LegacyBoot(bytes(header), kernel, ramdisk, b"", b"", page_size)
    header[BOOT.ID_OFFSET : BOOT.ID_OFFSET + 20] = BOOT.boot_id(provisional)
    return b"".join(
        (bytes(header), BOOT.padded(kernel, page_size), BOOT.padded(ramdisk, page_size))
    )


class LegacyBootRepackTests(unittest.TestCase):
    def test_repack_preserves_non_kernel_payload_and_updates_id(self):
        donor_data = synthetic_boot()
        donor = BOOT.verify(donor_data)
        output = BOOT.repack(donor, b"replacement-kernel")
        candidate = BOOT.verify(output)
        self.assertEqual(candidate.kernel, b"replacement-kernel")
        self.assertEqual(candidate.ramdisk, donor.ramdisk)
        self.assertEqual(candidate.second, donor.second)
        self.assertEqual(candidate.device_tree, donor.device_tree)
        self.assertNotEqual(output[BOOT.ID_OFFSET : BOOT.ID_OFFSET + 20],
                            donor_data[BOOT.ID_OFFSET : BOOT.ID_OFFSET + 20])

    def test_cli_refuses_to_overwrite_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            donor = root / "donor.img"
            kernel = root / "kernel"
            output = root / "candidate.img"
            donor.write_bytes(synthetic_boot())
            kernel.write_bytes(b"new")
            output.write_bytes(b"keep")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(donor), str(kernel), str(output)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("refusing to overwrite", result.stderr)
            self.assertEqual(output.read_bytes(), b"keep")

    def test_explicit_command_append_preserves_payloads(self):
        donor = BOOT.verify(synthetic_boot())
        output = BOOT.repack(donor, b"new", "snd_soc_tlv320aic3101.tater_hpf80=1")
        candidate = BOOT.verify(output)
        self.assertEqual(candidate.header_page[64:576].split(b"\0", 1)[0],
                         b"snd_soc_tlv320aic3101.tater_hpf80=1")
        self.assertEqual(candidate.ramdisk, donor.ramdisk)
        self.assertEqual(candidate.device_tree, donor.device_tree)

    def test_command_append_rejects_control_bytes_overflow_and_extended_donor(self):
        donor = BOOT.verify(synthetic_boot())
        for invalid in ("", "x\nreboot", "x\0y", "x" * 512):
            with self.assertRaises(ValueError):
                BOOT.repack(donor, b"new", invalid)
        from dataclasses import replace
        header = bytearray(donor.header_page)
        header[608] = ord("x")
        with self.assertRaises(ValueError):
            BOOT.repack(replace(donor, header_page=bytes(header)), b"new", "test=1")


if __name__ == "__main__":
    unittest.main()
