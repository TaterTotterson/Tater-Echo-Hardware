import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CAMERA = ROOT / "probes" / "mt8163-camera"


class CameraStateMachineTests(unittest.TestCase):
    def test_failure_paths_run_reverse_teardown(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "camera-state-selftest"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-std=c11",
                    str(CAMERA / "test_pattern_state.c"),
                    str(CAMERA / "state_machine_selftest.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            result = subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True
            )
            self.assertIn("self-test: ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
