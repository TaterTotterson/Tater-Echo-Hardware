import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CronosGPUGuardTests(unittest.TestCase):
    def test_exact_product_and_panel_are_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "guard.c"
            source.write_text(
                '#include "cronos_target.h"\n'
                'int main(int argc, char **argv) {\n'
                '  (void)cronos_gpu_target_allowed;\n'
                '  return argc == 4 && cronos_gpu_metadata_allowed(argv[1], argv[2], argv[3]) ? 0 : 2;\n'
                '}\n'
            )
            executable = directory / "guard"
            subprocess.run([
                "cc", "-Wall", "-Wextra", "-Werror", "-std=c11",
                "-I", str(ROOT / "probes/mt8163-gpu"), str(source),
                "-o", str(executable),
            ], check=True)
            command, size, depth = [directory / name for name in ("cmdline", "size", "depth")]
            for product, dimensions, bits, accepted in (
                ("androidboot.product=cronos", "480,2880", "32", True),
                ("androidboot.product=checkers", "480,2880", "32", False),
                ("androidboot.product=cronos-extra", "480,2880", "32", False),
                ("xandroidboot.product=cronos", "480,2880", "32", False),
                ("androidboot.product=cronos androidboot.product=checkers", "480,2880", "32", False),
                ("androidboot.product=cronos", "800,1280", "32", False),
                ("androidboot.product=cronos", "480,2880", "16", False),
                ("x" * 4096 + " androidboot.product=cronos", "480,2880", "32", False),
            ):
                with self.subTest(product=product[:80], dimensions=dimensions, bits=bits):
                    command.write_text(product + "\n")
                    size.write_text(dimensions + "\n")
                    depth.write_text(bits + "\n")
                    result = subprocess.run([str(executable), str(command), str(size), str(depth)])
                    self.assertEqual(result.returncode == 0, accepted)

    def test_cronos_preflight_precedes_hardware_access(self):
        for name in ("gles2_pbuffer_probe_android.c", "rgba_rotate_stream_android.c"):
            source = (ROOT / "probes/mt8163-gpu" / name).read_text()
            self.assertIn('#define GPU_TARGET "checkers"', source)
            self.assertIn('#define GPU_TARGET "cronos"', source)
            self.assertLess(source.index("if (!cronos_gpu_target_allowed())"), source.index("if (stat(MALI_NODE"))


if __name__ == "__main__":
    unittest.main()
