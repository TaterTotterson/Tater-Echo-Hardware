import ctypes
from pathlib import Path
import random
import subprocess
import tempfile
import unittest


class LinearRGBATests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(__file__).resolve().parents[1] / 'probes/mt8163-vcodec'
        library = Path(cls.temp.name) / 'linear.so'
        # Input source is provided to the compiler directly, not a generated file.
        subprocess.run(['cc', '-x', 'c', '-shared', '-fPIC', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-I'+str(root),
                        '-o', str(library), '-'], input=b'''#include "cronos_linear_rgba.h"
int convert(const unsigned char *p, size_t n, unsigned w, unsigned h,
            unsigned char *q, size_t m) {return cronos_i420_rgba(p,n,w,h,q,m);}
''', check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.convert = cls.lib.convert
        cls.convert.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                               ctypes.c_uint, ctypes.c_uint,
                               ctypes.c_void_p, ctypes.c_size_t]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_bt601_and_saturation(self):
        rng = random.Random(435)
        for w,h in [(2,2),(8,6),(128,128)]:
            pixels = w*h
            yuv = bytes(rng.randrange(256) for _ in range(pixels*3//2))
            expected = bytearray()
            clip = lambda x: max(0,min(255,x))
            for y in range(h):
                for x in range(w):
                    k = y//2*(w//2)+x//2
                    c,d,e = yuv[y*w+x]-16,yuv[pixels+k]-128,yuv[pixels*5//4+k]-128
                    expected.extend((clip((298*c+409*e+128)>>8),
                                     clip((298*c-100*d-208*e+128)>>8),
                                     clip((298*c+516*d+128)>>8),255))
            out = ctypes.create_string_buffer(pixels*4+16)
            self.assertEqual(self.convert(yuv,len(yuv),w,h,out,pixels*4),0)
            self.assertEqual(out.raw[:pixels*4],expected)
            self.assertEqual(out.raw[pixels*4:],bytes(16))

    def test_bad_geometry_lengths_and_nulls(self):
        out = ctypes.create_string_buffer(16)
        for p,n,w,h,m in [(None,6,2,2,16),(bytes(6),5,2,2,16),
                          (bytes(6),6,2,2,15),(bytes(6),6,3,2,16),
                          (bytes(6),6,2,3,16),(bytes(6),6,1922,2,16)]:
            self.assertEqual(self.convert(p,n,w,h,out,m),-1)
