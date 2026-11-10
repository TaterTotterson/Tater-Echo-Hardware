import json
import tempfile
import unittest
from pathlib import Path

from tater_re import ghidra
from tater_re.ghidra import find_analyze_headless, search_exports, status
from tater_re.workspace import Workspace, WorkspaceError


class GhidraTests(unittest.TestCase):
    def test_explicit_ghidra_home(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "support" / "analyzeHeadless"
            executable.parent.mkdir()
            executable.write_text("#!/bin/sh\n", encoding="utf-8")
            executable.chmod(0o755)
            self.assertEqual(find_analyze_headless(str(root)), executable.resolve())
            self.assertTrue(status(str(root))["available"])

    def test_invalid_explicit_path_does_not_fall_back(self) -> None:
        self.assertIsNone(find_analyze_headless("/definitely/missing/ghidra"))

    def test_searches_jsonl_exports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Workspace(Path(temporary) / "work")
            workspace.initialize()
            digest = "a" * 64
            export = workspace.root / "analysis" / "ghidra" / digest / "program.jsonl"
            export.parent.mkdir(parents=True)
            export.write_text(
                json.dumps({"record": "function", "name": "mtk_vcodec_open"}) + "\n",
                encoding="utf-8",
            )
            matches = search_exports(workspace, "VCODEC")
            self.assertEqual(matches[0]["name"], "mtk_vcodec_open")
            self.assertEqual(matches[0]["artifact_sha256"], digest)

    def test_rejects_missing_post_script_export(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            export = Path(temporary) / "missing.jsonl"
            log = Path(temporary) / "analyze.log"
            with self.assertRaisesRegex(WorkspaceError, "valid analysis export"):
                ghidra._validate_export(export, log)

    def test_accepts_program_export(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            export = Path(temporary) / "program.jsonl"
            log = Path(temporary) / "analyze.log"
            export.write_text('{"record":"program","name":"sample"}\n', encoding="utf-8")
            ghidra._validate_export(export, log)


if __name__ == "__main__":
    unittest.main()

