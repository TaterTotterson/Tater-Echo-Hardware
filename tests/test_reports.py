import tempfile
import unittest
from pathlib import Path

from tater_re.reports import capability_report, dependency_report
from tater_re.workspace import Workspace
from test_elf import elf32_header


class ReportTests(unittest.TestCase):
    def test_capability_groups_and_dependency_nodes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = root / "system" / "lib"
            inputs.mkdir(parents=True)
            (inputs / "libvcodecdrv.so").write_bytes(elf32_header())
            workspace = Workspace(root / "work")
            workspace.ingest(root / "system")
            capabilities = capability_report(workspace)
            dependencies = dependency_report(workspace)
            self.assertEqual(len(capabilities["groups"]["video-codec"]), 1)
            self.assertEqual(dependencies["elf_count"], 1)


if __name__ == "__main__":
    unittest.main()
