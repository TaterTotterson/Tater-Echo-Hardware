"""Compare the original fused converter with an independent scalar reference."""
import ctypes
import random
import subprocess
import tempfile
import unittest
from pathlib import Path


class BayerConversionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(__file__).resolve().parents[1] / "probes/mt8163-vcodec"
        library = Path(cls.temp.name) / "bayer.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread", str(root / "tater_bayer10.c"), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.convert = cls.lib.tater_bayer10_yuv420
        cls.convert.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                               ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p,
                               ctypes.c_void_p, ctypes.c_size_t]
        cls.convert.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_pixels_and_mirrored_edges_match_reference(self):
        rng = random.Random(912)
        for width, height in [(4, 2), (8, 6), (16, 16), (32, 18)]:
            with self.subTest(width=width, height=height):
                samples = [rng.randrange(1024) for _ in range(width * height)]
                raw = bytearray()
                for i in range(0, len(samples), 4):
                    packed = sum(samples[i+n] << (10*n) for n in range(4))
                    raw.extend(packed.to_bytes(5, "little"))
                tables = bytes(rng.randrange(256) for _ in range(6144))
                rgb = []
                def at(x, y):
                    x = 1 if x < 0 else width-2 if x >= width else x
                    y = 1 if y < 0 else height-2 if y >= height else y
                    return samples[y*width+x]
                for y in range(height):
                    for x in range(width):
                        c = at(x, y)
                        cross = (at(x-1,y)+at(x+1,y)+at(x,y-1)+at(x,y+1))//4
                        diagonal = (at(x-1,y-1)+at(x+1,y-1)+at(x-1,y+1)+at(x+1,y+1))//4
                        horizontal = (at(x-1,y)+at(x+1,y))//2
                        vertical = (at(x,y-1)+at(x,y+1))//2
                        r,g,b = [(diagonal,cross,c),(vertical,c,horizontal),
                                 (horizontal,c,vertical),(c,cross,diagonal)][(y%2)*2+x%2]
                        rgb.append((tables[2*r],tables[2048+2*g],tables[4096+2*b]))
                expected = bytearray(width*height*3//2)
                for i, (r,g,b) in enumerate(rgb):
                    expected[i] = 16+((66*r+129*g+25*b+128)>>8)
                for y in range(0,height,2):
                    for x in range(0,width,2):
                        block = [rgb[(y+dy)*width+x+dx] for dy in range(2) for dx in range(2)]
                        u = sum(-38*r-74*g+112*b for r,g,b in block)
                        v = sum(112*r-94*g-18*b for r,g,b in block)
                        j = (y//2)*(width//2)+x//2
                        expected[width*height+j] = 128+((u+512)>>10)
                        expected[width*height*5//4+j] = 128+((v+512)>>10)
                result = ctypes.create_string_buffer(len(expected))
                scratch = (ctypes.c_uint16 * len(samples))()
                self.assertEqual(self.convert(bytes(raw), len(raw), tables, width, height, scratch, result, len(expected)), 0)
                self.assertEqual(result.raw, expected)

    def test_invalid_lengths_and_geometry_fail_closed(self):
        raw, tables = bytes(40), bytes(6144)
        scratch = (ctypes.c_uint16 * 64)()
        out = ctypes.create_string_buffer(96)
        for width,height,size,outsize in [(8,4,39,48),(8,4,40,47),(7,4,35,48),(8,3,30,36),(1284,4,40,96)]:
            self.assertEqual(self.convert(raw,size,tables,width,height,scratch,out,outsize),-1)


if __name__ == "__main__":
    unittest.main()
