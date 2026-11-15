"""Generated-only GPU conversion contract; no device access in these tests."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "probes/mt8163-gpu/bayer10_offscreen_android.c"


class GpuBayerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        wrapper = Path(cls.tmp.name) / "reference.c"
        wrapper.write_text(
            '#define TATER_GPU_BAYER_REFERENCE_ONLY\n'
            f'#include "{SOURCE}"\n'
            'void fixture(unsigned n,void *raw,void *lut,void *samples,void *rgba,void *yuv,void *scratch){'
            'generate(n,raw,lut,samples);reference_rgba(samples,lut,rgba);'
            'scaled_bayer10_yuv420(raw,RAW_BYTES,lut,SW,SH,DW,DH,1,scratch,yuv,YUV_BYTES);'
            'struct difference d=compare(rgba,rgba,RGBA_BYTES);(void)d;(void)milliseconds();}\n'
        )
        library = Path(cls.tmp.name) / "reference.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-std=c11", "-O2", "-Wall",
                        "-Wextra", "-Werror", "-pthread", str(wrapper), "-lm", "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.fixture.argtypes = [ctypes.c_uint] + [ctypes.c_void_p]*6

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_generated_primary_colors_and_i420_order(self):
        raw = ctypes.create_string_buffer(1600*1200*5//4)
        lut = ctypes.create_string_buffer(6144)
        samples = ctypes.create_string_buffer(1600*1200*2)
        scratch = ctypes.create_string_buffer(1600*1200*2)
        rgba = ctypes.create_string_buffer(640*480*4+16)
        yuv = ctypes.create_string_buffer(640*480*3//2+16)
        for pattern, color in [(0, (128,128,128)), (1, (255,0,0)),
                               (2, (0,255,0)), (3, (0,0,255))]:
            with self.subTest(pattern=pattern):
                self.lib.fixture(pattern,raw,lut,samples,rgba,yuv,scratch)
                r,g,b=color
                self.assertEqual(rgba.raw,bytes((r,g,b,255))*(640*480)+bytes(16))
                yy=16+((66*r+129*g+25*b+128)>>8)
                u=128+((-38*r-74*g+112*b+128)>>8)
                v=128+((112*r-94*g-18*b+128)>>8)
                self.assertEqual(yuv.raw,bytes([yy])*(640*480)+bytes([u])*(640*480//4)+bytes([v])*(640*480//4)+bytes(16))

    def test_target_gate_and_generated_only_scope(self):
        source = SOURCE.read_text()
        main = source[source.index("int main("):]
        self.assertLess(main.index("cronos_gpu_target_allowed()"),main.index("initialize(&g)"))
        self.assertIn('alarm(45)',main)
        self.assertIn('private-pbuffer-no-camera',main)
        for forbidden in ['ioctl(', '/dev/camera', '/dev/graphics', '/dev/fb', 'system(', 'execv(', 'fopen(']:
            self.assertNotIn(forbidden,source)
        self.assertIn('g->glDisable(GL_DITHER)',source)
        self.assertIn('GL_FRAGMENT_SHADER,GL_HIGH_FLOAT',source)
        self.assertIn('g->glReadPixels(0,0,DW/4,DH*3/2',source)
        self.assertIn('ITERATIONS > 128',source)
        # Exact binary shifts/multipliers: exp2/division approximation failed
        # generated gradients and noise on the real ES2 GPU.
        self.assertNotIn('exp2(',source)
        self.assertIn('float x=floor(p.x*2.5)',source)
        self.assertIn('return (v>>uint(bit&7))&1023u',source)

    def test_research_target_is_not_a_release_dependency(self):
        makefile = (SOURCE.parent / 'Makefile').read_text()
        self.assertIn('android-bayer-bench: $(BAYER_BENCH)',makefile)
        release = makefile.split('android-api28:',1)[1].split('\n',1)[0]
        self.assertNotIn('BAYER',release)


if __name__ == "__main__":
    unittest.main()
