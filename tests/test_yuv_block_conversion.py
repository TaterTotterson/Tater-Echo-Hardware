import ctypes
import random
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VCODEC = ROOT / "probes" / "mt8163-vcodec"


class YuvBlockConversionTests(unittest.TestCase):
    def test_vector_conversion_matches_scalar_bytes_and_clipped_tails(self):
        rng = random.Random(4719)
        with tempfile.TemporaryDirectory() as directory:
            converters = []
            for name, options in (("optimized", []), ("scalar", ["-DTATER_MTK_YUV_FORCE_SCALAR"])):
                output = Path(directory) / (name + ".so")
                subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror", *options,
                                str(VCODEC / "mtk_yuv420_block.c"), "-o", str(output)], check=True)
                function = ctypes.CDLL(str(output)).tater_mtk_yuv420_block_to_rgba8888
                function.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint,
                                     ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t]
                function.restype = ctypes.c_int
                converters.append(function)
            for bw, bh, vw, vh in ((16,32,16,32),(32,64,30,61),(48,64,33,37),(128,96,127,93),(1280,736,1280,720)):
                for kind in ("random", "minimum", "maximum"):
                    size, stride = bw*bh*3//2, vw*4+12
                    source = bytes(rng.randrange(256) for _ in range(size)) if kind == "random" else bytes([0 if kind == "minimum" else 255])*size
                    results = []
                    for convert in converters:
                        out = ctypes.create_string_buffer(stride*vh)
                        self.assertEqual(convert(source,len(source),bw,bh,vw,vh,out,stride,len(out)),0)
                        results.append(out.raw)
                    self.assertEqual(results[0],results[1],(bw,bh,vw,vh,kind))

    def test_converter_detiles_to_rgba_and_planar_yuv(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "yuv-block-selftest"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-std=c11",
                    str(VCODEC / "mtk_yuv420_block.c"),
                    str(VCODEC / "yuv_block_selftest.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            result = subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True
            )
            self.assertIn("conversion self-test: ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
