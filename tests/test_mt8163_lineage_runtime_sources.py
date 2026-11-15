import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from scripts.assemble_mt8163_lineage_runtime import LOCK_PATH, ordinary_source, relative_path, validate_inputs
from scripts.build_checkers_lineage import digest

ROOT = Path(__file__).resolve().parents[1]


class MT8163LineageSourceTests(unittest.TestCase):
    def test_source_inventory_recreates_exact_tree_identities(self):
        lock = json.loads(LOCK_PATH.read_text())
        for relative, expected in lock["sources"].items():
            self.assertEqual(digest(ROOT / relative), expected, relative)
        for name, count, expected in (
            ("gpu", 34, "bc75d30a7be167b827e89b1d7629ec5dc4b528c99106d0b1e9beed02b4815561"),
            ("codec", 66, "8564d383faba23c32545037f62a7ab9ddddf227a61790470dbc883e6eb871f0a"),
        ):
            runtime = lock["runtimes"][name]
            self.assertEqual(len(runtime["files"]), count)
            tree = hashlib.sha256()
            for relative in sorted(runtime["files"], key=lambda n: (n != "linker", n)):
                entry = runtime["files"][relative]
                self.assertNotIn(".tater-re", entry["path"])
                self.assertIn(entry["kind"], ("sdk24", "vndk28", "vendor", "shims"))
                tree.update(relative.encode() + b"\0" + entry["sha256"].encode() + b"\n")
            self.assertEqual(tree.hexdigest(), expected)
            self.assertEqual(runtime["tree_sha256"], expected)

    def test_input_hash_and_symlink_checks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source = root / "library"
            source.write_bytes(b"example")
            expected = hashlib.sha256(b"example").hexdigest()
            self.assertEqual(ordinary_source(root, "library", expected), source)
            with self.assertRaises(ValueError):
                ordinary_source(root, "library", "0" * 64)
            (root / "link").symlink_to(source)
            with self.assertRaises(ValueError):
                ordinary_source(root, "link", expected)
            real = root / "real"
            real.mkdir()
            (real / "library").write_bytes(b"example")
            (root / "linked-directory").symlink_to(real, target_is_directory=True)
            with self.assertRaises(ValueError):
                ordinary_source(root, "linked-directory/library", expected)

    def test_unsafe_paths_and_unknown_sources_are_rejected(self):
        for value in ("", "/absolute", "../outside", "lib/../outside", "./library"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                relative_path(value)
        lock = {"schema": 1, "sources": {}, "runtimes": {
            "gpu": {"files": {"linker": {"kind": "unknown", "path": "linker", "sha256": "0" * 64}}},
            "codec": {"files": {}},
        }}
        with self.assertRaises(ValueError):
            validate_inputs(lock, {})
        bad = copy.deepcopy(lock)
        bad["runtimes"]["gpu"]["files"] = {"../outside": {"kind": "shims", "path": "library"}}
        with self.assertRaises(ValueError):
            validate_inputs(bad, {})


if __name__ == "__main__":
    unittest.main()
