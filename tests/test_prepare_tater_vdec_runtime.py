import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts.prepare_tater_vdec_runtime import (
    RuntimePreparationError,
    file_sha256,
    prepare_runtime,
    runtime_tree_identity,
)


class PrepareTaterVDECRuntimeTests(unittest.TestCase):
    def fixture(self, root: Path) -> tuple[Path, Path, str, str]:
        source = root / "source"
        (source / "lib").mkdir(parents=True)
        (source / "linker").write_bytes(b"linker")
        (source / "lib" / "decoder.so").write_bytes(b"decoder")
        helper = root / "helper"
        helper.write_bytes(b"open-helper")
        tree, count, _ = runtime_tree_identity(source)
        self.assertEqual(count, 2)
        return source, helper, tree, file_sha256(helper)

    def test_prepares_new_read_only_runtime_with_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, helper, tree, helper_hash = self.fixture(root)
            output = root / "output"
            manifest = prepare_runtime(
                source,
                helper,
                output,
                expected_tree_sha256=tree,
                expected_file_count=2,
                expected_helper_sha256=helper_hash,
            )
            self.assertEqual(manifest["tree_sha256"], tree)
            self.assertEqual((output / "lib" / "decoder.so").read_bytes(), b"decoder")
            self.assertEqual((output / "tater-vdec").read_bytes(), b"open-helper")
            saved = json.loads((output / "runtime.json").read_text(encoding="utf-8"))
            self.assertEqual(saved, manifest)

    def test_rejects_wrong_identity_and_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, helper, tree, helper_hash = self.fixture(root)
            with self.assertRaises(RuntimePreparationError):
                prepare_runtime(
                    source,
                    helper,
                    root / "bad",
                    expected_tree_sha256="0" * 64,
                    expected_file_count=2,
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
                    expected_helper_sha256=helper_hash,
                )


if __name__ == "__main__":
    unittest.main()
