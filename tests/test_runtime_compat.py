import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from scripts import build_runtime_compat as compat


class RuntimeCompatTests(unittest.TestCase):
    def test_original_sources_and_output_inventory_are_pinned(self):
        lock = json.loads(compat.LOCK.read_text())
        self.assertEqual(len(lock["files"]), 9)
        self.assertEqual(len(lock["sync_sources"]), 5)
        for name, expected in lock["sources"].items():
            self.assertEqual(compat.digest(compat.ROOT / name), expected, name)
        self.assertNotIn("libGLES_mali.so", lock["files"])
        self.assertEqual(lock["gles_link_sha256"], "dcb3eac3dd0f220124b39c5a44b0e8a4d5caeb475fe9465db45d8f5f9a75f04d")

    def test_existing_output_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "output exists"):
                compat.build(root, root, root / "libGLES_mali.so", root)

    def test_input_digest_refuses_links(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.write_bytes(b"original")
            self.assertEqual(compat.digest(source), hashlib.sha256(b"original").hexdigest())
            link = root / "link"
            link.symlink_to(source)
            with self.assertRaises(ValueError):
                compat.digest(link)


if __name__ == "__main__":
    unittest.main()
