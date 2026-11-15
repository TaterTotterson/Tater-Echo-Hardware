import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts.prepare_tater_gpu_runtime import (
    RuntimePreparationError,
    file_sha256,
    prepare_runtime,
    runtime_tree_identity,
)


class PrepareTaterGPURuntimeTests(unittest.TestCase):
    def test_library_only_manifest_excludes_helper(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, helper, tree, helper_hash, size = self.fixture(root)
            manifest = prepare_runtime(source, helper, root / "output", expected_tree_sha256=tree,
                                       expected_file_count=2, expected_runtime_bytes=size,
                                       expected_helper_sha256=helper_hash, include_helper=False)
            self.assertEqual(2, manifest["schema"])
            self.assertNotIn("helper_sha256", manifest)
            self.assertFalse((root / "output/tater-gpu-rotate").exists())

    def fixture(self, root: Path) -> tuple[Path, Path, str, str, int]:
        source = root / "source"
        (source / "lib").mkdir(parents=True)
        (source / "linker").write_bytes(b"linker")
        (source / "lib" / "gpu.so").write_bytes(b"gpu")
        helper = root / "helper"
        helper.write_bytes(b"open-helper")
        tree, count, size = runtime_tree_identity(source)
        self.assertEqual(count, 2)
        return source, helper, tree, file_sha256(helper), size

    def test_prepares_new_read_only_runtime_with_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, helper, tree, helper_hash, size = self.fixture(root)
            output = root / "output"
            manifest = prepare_runtime(
                source,
                helper,
                output,
                expected_tree_sha256=tree,
                expected_file_count=2,
                expected_runtime_bytes=size,
                expected_helper_sha256=helper_hash,
            )
            self.assertEqual(manifest["tree_sha256"], tree)
            self.assertEqual((output / "lib" / "gpu.so").read_bytes(), b"gpu")
            self.assertEqual((output / "tater-gpu-rotate").read_bytes(), b"open-helper")
            saved = json.loads((output / "runtime.json").read_text(encoding="utf-8"))
            self.assertEqual(saved, manifest)

    def test_rejects_wrong_identity_size_and_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, helper, tree, helper_hash, size = self.fixture(root)
            with self.assertRaises(RuntimePreparationError):
                prepare_runtime(
                    source,
                    helper,
                    root / "bad",
                    expected_tree_sha256="0" * 64,
                    expected_file_count=2,
                    expected_runtime_bytes=size,
                    expected_helper_sha256=helper_hash,
                )
            with self.assertRaises(RuntimePreparationError):
                prepare_runtime(
                    source,
                    helper,
                    root / "bad-size",
                    expected_tree_sha256=tree,
                    expected_file_count=2,
                    expected_runtime_bytes=size + 1,
                    expected_helper_sha256=helper_hash,
                )
            output = root / "existing"
            output.mkdir()
            with self.assertRaises(RuntimePreparationError):
                prepare_runtime(
                    source,
                    helper,
                    output,
                    expected_tree_sha256=tree,
                    expected_file_count=2,
                    expected_runtime_bytes=size,
                    expected_helper_sha256=helper_hash,
                )


if __name__ == "__main__":
    unittest.main()
