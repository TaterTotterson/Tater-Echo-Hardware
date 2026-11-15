"""Public repository metadata and custody boundaries; no hardware access."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PublicationTests(unittest.TestCase):
    def test_name_license_and_compatibility_cli(self):
        metadata = (ROOT / "pyproject.toml").read_text()
        self.assertIn('name = "tater-echo-hardware"', metadata)
        self.assertIn('license = "AGPL-3.0-only"', metadata)
        self.assertIn('tater-re = "tater_re.cli:main"', metadata)
        license_text = (ROOT / "LICENSE").read_text()
        self.assertIn("GNU AFFERO GENERAL PUBLIC LICENSE", license_text)
        self.assertIn("Version 3, 19 November 2007", license_text)
        self.assertGreater(len(license_text), 34000)

    def test_separate_component_license_notices_are_retained(self):
        self.assertIn("MIT License", (ROOT / "LICENSES/MIT.txt").read_text())
        self.assertIn("Version 2, June 1991", (ROOT / "LICENSES/GPL-2.0-only.txt").read_text())
        notices = (ROOT / "NOTICE.md").read_text()
        for license_id in ("AGPL-3.0-only", "MIT", "GPL-2.0-only"):
            self.assertIn(license_id, notices)

    def test_private_workspaces_and_generated_binaries_are_ignored(self):
        paths = (".tater-re/private.bin", "private/runtime.so", "firmware/boot.img",
                 "blobs/vendor.so", "captures/camera.raw", "probes/mt8163-gpu/build/helper")
        result = subprocess.run(["git", "check-ignore", "--", *paths], cwd=ROOT,
                                text=True, capture_output=True, check=True)
        self.assertEqual(set(result.stdout.splitlines()), set(paths))


if __name__ == "__main__":
    unittest.main()
