import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class CrownGuardTests(unittest.TestCase):
    def test_exact_crown_product_and_native_panel(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "guard.c"
            source.write_text('#include "crown_target.h"\n'
                              'int main(int argc,char **argv) { (void)crown_gpu_target_allowed; '
                              'return argc==4 && crown_gpu_metadata_allowed(argv[1],argv[2],argv[3]) ? 0 : 2; }\n')
            binary = root / "guard"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-std=c11", "-I", str(ROOT / "probes/mt8163-gpu"), str(source), "-o", str(binary)], check=True)
            command, size, depth = [root / name for name in ("command", "size", "depth")]
            for product, dimensions, bits, accepted in (
                ("androidboot.product=crown", "800,3840", "32", True),
                ("androidboot.product=cronos", "800,3840", "32", False),
                ("androidboot.product=crown-extra", "800,3840", "32", False),
                ("xandroidboot.product=crown", "800,3840", "32", False),
                ("androidboot.product=crown androidboot.product=crown", "800,3840", "32", False),
                ("androidboot.product=crown", "480,2880", "32", False),
                ("androidboot.product=crown", "800,3840", "16", False),
                ("x" * 4096 + " androidboot.product=crown", "800,3840", "32", False),
            ):
                with self.subTest(product=product[:70], dimensions=dimensions, bits=bits):
                    command.write_text(product + "\n")
                    size.write_text(dimensions + "\n")
                    depth.write_text(bits + "\n")
                    self.assertEqual(subprocess.run([str(binary), str(command), str(size), str(depth)]).returncode == 0, accepted)

    def test_crown_gpu_gate_precedes_device_access(self):
        source = (ROOT / "probes/mt8163-gpu/rgba_rotate_stream_android.c").read_text()
        self.assertLess(source.index("if (!crown_gpu_target_allowed())"), source.index("if (stat(MALI_NODE"))


if __name__ == "__main__":
    unittest.main()
