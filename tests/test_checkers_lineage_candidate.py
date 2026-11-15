import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from scripts.build_checkers_lineage import DEFAULT_LOCK, validate_lock

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "probes/checkers-lineage-codec"


class CheckersLineageCandidateTests(unittest.TestCase):
    def test_candidate_is_locked_and_does_not_replace_published_sources(self):
        lock = json.loads(DEFAULT_LOCK.read_text())
        validate_lock(lock)
        self.assertFalse(lock["release_qualified"])
        bad = copy.deepcopy(lock)
        bad["release_qualified"] = True
        with self.assertRaises(ValueError):
            validate_lock(bad)
        bad = copy.deepcopy(lock)
        bad["sources"]["../outside"] = "0" * 64
        with self.assertRaises(ValueError):
            validate_lock(bad)

    def test_metadata_gate_is_exact_and_bounded(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "guard.c"
            source.write_text('#include "checkers_target.h"\n'
                              'int main(int argc, char **argv) {\n'
                              ' (void)checkers_lineage_target_allowed;\n'
                              ' return argc == 4 && checkers_lineage_metadata_allowed(argv[1],argv[2],argv[3]) ? 0 : 2;\n}\n')
            executable = root / "guard"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-std=c11", "-I", str(PROBE), str(source), "-o", str(executable)], check=True)
            command, size, depth = [root / n for n in ("command", "size", "depth")]
            for product, dimensions, bits, accepted in (
                ("androidboot.product=checkers", "480,2880", "32", True),
                ("console=tty0 androidboot.product=checkers quiet", "480,2880", "32", True),
                ("androidboot.product=cronos", "480,2880", "32", False),
                ("androidboot.product=checkers-extra", "480,2880", "32", False),
                ("xandroidboot.product=checkers", "480,2880", "32", False),
                ("androidboot.product=checkers androidboot.product=checkers", "480,2880", "32", False),
                ("androidboot.product=checkers", "800,3840", "32", False),
                ("androidboot.product=checkers", "480,2880", "16", False),
                ("androidboot.product=checkers\0 androidboot.product=cronos", "480,2880", "32", False),
                ("x" * 4096 + " androidboot.product=checkers", "480,2880", "32", False),
            ):
                with self.subTest(product=product[:80], dimensions=dimensions, bits=bits):
                    command.write_text(product + "\n")
                    size.write_text(dimensions + "\n")
                    depth.write_text(bits + "\n")
                    result = subprocess.run([str(executable), str(command), str(size), str(depth)])
                    self.assertEqual(result.returncode == 0, accepted)

    def test_decoder_reuses_linear_abi_and_encoder_preserves_camera_conversion(self):
        makefile = (PROBE / "Makefile").read_text()
        self.assertIn("-DTATER_CRONOS_ABI", makefile)
        self.assertNotIn("omx_native_buffer_android.cpp", makefile)
        self.assertNotIn("-lui", makefile)
        self.assertIn("-Dmain=tater_checkers_encoder_main", makefile)
        self.assertIn("$(CODEC)/venc_probe_android.c", makefile)
        self.assertIn("-Wl,--wrap=dlclose", makefile)
        self.assertIn("/data/tater-vdec-lineage-1/linker", makefile)
        source = (PROBE / "decoder_main.c").read_text()
        self.assertLess(source.index("if (!checkers_lineage_target_allowed())"), source.index("tater_vdec_stream_rgba8888_v2("))
        self.assertIn("TVO2-header-and-timestamped-rgba", source)

    def test_decoder_numeric_arguments_are_bounded_without_hardware(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "arguments.c"
            source.write_text(
                '#define main candidate_main\n#include "decoder_main.c"\n#undef main\n'
                'int tater_vdec_decode_batch(const struct tater_vdec_batch *r) {(void)r;return 1;}\n'
                'int tater_vdec_stream_rgba8888(const char *c,uint32_t w,uint32_t h,uint32_t f,int i,int o)\n'
                '{(void)c;(void)w;(void)h;(void)f;(void)i;(void)o;return 1;}\n'
                'int tater_vdec_stream_rgba8888_v2(const char *c,uint32_t w,uint32_t h,uint32_t f,int i,int o)\n'
                '{return tater_vdec_stream_rgba8888(c,w,h,f,i,o);}\n'
                'int main(void) {uint32_t n;\n'
                ' const char *bad[]={"", "-1", "+1", " 1", "12x", "4294967296", "999999999999999999999999999999"};\n'
                ' for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) if(!number(bad[i],&n))return 1;\n'
                ' return number("4294967295",&n)||n!=UINT32_MAX||number("768",&n)||n!=768;\n}\n')
            executable = root / "arguments"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-std=c11", "-I", str(PROBE), str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
