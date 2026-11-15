"""Screen-sized conversion: independent CFA reference, full field, bounded output."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest


class BalancedBayerTests(unittest.TestCase):
    def test_both_sensor_patterns_and_aspect_ratios(self):
        root = Path(__file__).resolve().parents[1] / "probes/mt8163-vcodec"
        for board, sw, sh, dw, dh, red_first in [
            ("tater", 1280, 720, 768, 432, False),
            ("cronos", 1600, 1200, 640, 480, True),
        ]:
            with self.subTest(board=board), tempfile.TemporaryDirectory() as tmp:
                library = Path(tmp) / "convert.so"
                subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                                "-pthread", str(root / (board + "_bayer10.c")), "-o", str(library)], check=True)
                lib = ctypes.CDLL(str(library))
                convert = getattr(lib, board + "_bayer10_balanced_yuv420")
                convert.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
                def sample(x, y):
                    x = 1 if x < 0 else sw - 2 if x >= sw else x
                    y = 1 if y < 0 else sh - 2 if y >= sh else y
                    return (x * 11 + y * 37) % 1024
                raw = bytearray()
                for y in range(sh):
                    for x in range(0, sw, 4):
                        raw.extend(sum(sample(x+i, y) << (10*i) for i in range(4)).to_bytes(5, "little"))
                tables = bytes(i // 8 for _ in range(3) for i in range(2048))
                scratch = (ctypes.c_uint16 * (sw * sh))()
                size = dw * dh * 3 // 2
                output = ctypes.create_string_buffer(size + 16)
                self.assertEqual(convert(bytes(raw), len(raw), tables, scratch, output, size), 0)
                self.assertEqual(output.raw[size:], bytes(16))
                def rgb(dx, dy):
                    x, y = dx * sw // dw, dy * sh // dh
                    c = sample(x, y)
                    cross = sum(sample(x+a, y+b) for a,b in [(-1,0),(1,0),(0,-1),(0,1)]) // 4
                    diagonal = sum(sample(x+a, y+b) for a in [-1,1] for b in [-1,1]) // 4
                    horizontal = (sample(x-1,y) + sample(x+1,y)) // 2
                    vertical = (sample(x,y-1) + sample(x,y+1)) // 2
                    r,g,b = [(c,cross,diagonal),(horizontal,c,vertical),
                             (vertical,c,horizontal),(diagonal,cross,c)][(y%2)*2+x%2]
                    if not red_first:
                        r,b = b,r
                    return tables[2*r], tables[2048+2*g], tables[4096+2*b]
                # Corners, every worker boundary, and spatially varying samples.
                for y in sorted({0, 2, dh//4-2, dh//4, dh//2, 3*dh//4, dh-2}):
                    for x in sorted(set(range(0, dw, 14)) | {dw-2}):
                        block = [rgb(x+i,y+j) for j in range(2) for i in range(2)]
                        for j in range(2):
                            for i in range(2):
                                r,g,b = block[j*2+i]
                                self.assertEqual(output.raw[(y+j)*dw+x+i], 16+((66*r+129*g+25*b+128)>>8))
                        u = 128 + ((sum(-38*r-74*g+112*b for r,g,b in block)+512)>>10)
                        v = 128 + ((sum(112*r-94*g-18*b for r,g,b in block)+512)>>10)
                        offset = (y//2)*(dw//2)+x//2
                        self.assertEqual(output.raw[dw*dh+offset], u)
                        self.assertEqual(output.raw[dw*dh*5//4+offset], v)
                self.assertEqual(convert(bytes(raw), len(raw)-1, tables, scratch, output, size), -1)
                self.assertEqual(convert(bytes(raw), len(raw), tables, scratch, output, size-1), -1)
                self.assertEqual(convert(None, len(raw), tables, scratch, output, size), -1)
