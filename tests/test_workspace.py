import tempfile
import unittest
from pathlib import Path

from tater_re.workspace import Workspace, WorkspaceError
from test_elf import elf32_header


class WorkspaceTests(unittest.TestCase):
    def test_ingest_deduplicates_content_and_keeps_locations(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = root / "firmware"
            inputs.mkdir()
            (inputs / "vendor").mkdir()
            content = elf32_header()
            (inputs / "libcodec.so").write_bytes(content)
            (inputs / "vendor" / "libcodec-copy.so").write_bytes(content)
            (inputs / "readme.txt").write_text("sample", encoding="utf-8")

            workspace = Workspace(root / "work")
            result = workspace.ingest(inputs)
            records = workspace.records()

            self.assertEqual(result["scanned"], 3)
            self.assertEqual(result["added"], 2)
            self.assertEqual(len(records), 2)
            elf_records = [record for record in records if record[1]["kind"] == "elf"]
            self.assertEqual(len(elf_records[0][1]["locations"]), 2)
            self.assertEqual(len(workspace.search("libcodec")), 1)
            self.assertEqual(len(workspace.search("arm")), 1)

    def test_resolve_rejects_missing_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Workspace(Path(temporary) / "work")
            workspace.initialize()
            with self.assertRaises(WorkspaceError):
                workspace.resolve("deadbeef")


if __name__ == "__main__":
    unittest.main()

