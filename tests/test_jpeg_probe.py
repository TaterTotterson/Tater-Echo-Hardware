import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class JpegProbeTests(unittest.TestCase):
    def test_describe_is_host_safe_and_exact(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("C compiler is unavailable")
        source = Path(__file__).resolve().parents[1] / "probes" / "mt8163-jpeg" / "probe.c"
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "probe"
            subprocess.run(
                [compiler, "-O2", "-Wall", "-Wextra", "-Werror", "-std=c11", source, "-o", executable],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run(
                [executable, "--describe"], check=True, capture_output=True, text=True
            )
        description = json.loads(result.stdout)
        self.assertEqual(description["abi"], "mt8163-arm32")
        self.assertEqual(description["output_size"], 8)
        self.assertEqual(description["config_size"], 208)
        self.assertEqual(description["wait"], "0xc0087805")

    def test_active_mode_refuses_missing_acknowledgements(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("C compiler is unavailable")
        source = Path(__file__).resolve().parents[1] / "probes" / "mt8163-jpeg" / "probe.c"
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "probe"
            subprocess.run([compiler, "-std=c11", source, "-o", executable], check=True)
            result = subprocess.run(
                [executable, "--execute-init-cycle"], capture_output=True, text=True
            )
        self.assertEqual(result.returncode, 2)
        self.assertIn("refusing", result.stderr)

    def test_active_mode_refuses_non_checkers_host_with_acknowledgements(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("C compiler is unavailable")
        source = Path(__file__).resolve().parents[1] / "probes" / "mt8163-jpeg" / "probe.c"
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "probe"
            subprocess.run([compiler, "-std=c11", source, "-o", executable], check=True)
            result = subprocess.run(
                [
                    executable,
                    "--target",
                    "checkers",
                    "--acknowledge-hardware-ioctl",
                    "--execute-init-cycle",
                ],
                capture_output=True,
                text=True,
            )
        self.assertEqual(result.returncode, 2)
        self.assertIn("device is not compatible", result.stderr)


if __name__ == "__main__":
    unittest.main()
