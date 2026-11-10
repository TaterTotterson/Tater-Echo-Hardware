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
            (inputs / "device.key").write_text("secret", encoding="utf-8")

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
            self.assertEqual(len(workspace.search("device.key")), 0)

    def test_resolve_rejects_missing_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Workspace(Path(temporary) / "work")
            workspace.initialize()
            with self.assertRaises(WorkspaceError):
                workspace.resolve("deadbeef")

    def test_skips_direct_sensitive_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            key = root / "device.key"
            key.write_text("secret", encoding="utf-8")
            workspace = Workspace(root / "work")
            result = workspace.ingest(key)
            self.assertEqual(result["scanned"], 0)
            self.assertEqual(workspace.records(), [])

    def test_reingest_upgrades_generic_classification(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "gpt.img"
            image.write_bytes(bytes(512) + b"EFI PART" + bytes(1536))
            workspace = Workspace(root / "work")
            workspace.ingest(image)
            manifest = workspace.load()
            digest = next(iter(manifest["artifacts"]))
            manifest["artifacts"][digest]["kind"] = "data"
            workspace.save(manifest)

            workspace.ingest(image)
            self.assertEqual(workspace.resolve(digest)[1]["kind"], "gpt")


if __name__ == "__main__":
    unittest.main()

