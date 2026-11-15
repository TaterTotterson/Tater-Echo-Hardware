import hashlib
from pathlib import Path
import tempfile
import unittest

from scripts.build_tater_hardware import verify_sources
from scripts.prepare_tater_hardware_runtime import private_output


class HardwareBuildTests(unittest.TestCase):
    def test_source_identity_and_unsafe_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            (source / "original.c").write_bytes(b"original")
            lock = {"sources": {"original.c": hashlib.sha256(b"original").hexdigest()}}
            verify_sources(source, lock)
            (source / "original.c").write_bytes(b"modified")
            with self.assertRaises(ValueError):
                verify_sources(source, lock)
            for relative in ("../outside", "/absolute"):
                with self.assertRaises(ValueError):
                    verify_sources(source, {"sources": {relative: "0" * 64}})

    def test_private_output_refuses_existing_and_public_release_destinations(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                private_output(root)
            for relative in ("release/private", "dist/private", "factory/payload/private"):
                with self.assertRaises(ValueError):
                    private_output(root / relative)
            private_output(root / "owner-private")


if __name__ == "__main__":
    unittest.main()
