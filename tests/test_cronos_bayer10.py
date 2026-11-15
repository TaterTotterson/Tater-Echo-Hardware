import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest


class CronosBayerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        source=Path(__file__).resolve().parents[1]/'probes/mt8163-vcodec/cronos_bayer10.c'
        lib=Path(cls.temp.name)/'bayer.so'
        subprocess.run(['cc','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',
                        '-pthread',str(source),'-o',str(lib)],check=True)
        cls.lib=ctypes.CDLL(str(lib))
        cls.convert=cls.lib.cronos_bayer10_yuv420
        cls.convert.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,
                              ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t]

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def test_red_first_constant_channels_and_full_field(self):
        # Flat red/green/blue photosites give the same RGB at every pixel,
        # including mirrored edges. Red/blue reversal would fail U/V checks.
        width,height=1600,1200
        row0=sum(v<<(10*i) for i,v in enumerate([800,400,800,400])).to_bytes(5,'little')*(width//4)
        row1=sum(v<<(10*i) for i,v in enumerate([400,100,400,100])).to_bytes(5,'little')*(width//4)
        raw=(row0+row1)*(height//2)
        tables=bytes(i//8 for _ in range(3) for i in range(2048))
        scratch=(ctypes.c_uint16*(width*height))()
        pixels=1280*960
        out=ctypes.create_string_buffer(pixels*3//2+16)
        self.assertEqual(self.convert(raw,len(raw),tables,scratch,out,pixels*3//2),0)
        r,g,b=200,100,25
        y=16+((66*r+129*g+25*b+128)>>8)
        u=128+((-38*r-74*g+112*b+128)>>8)
        v=128+((112*r-94*g-18*b+128)>>8)
        self.assertEqual(out.raw[:pixels],bytes([y])*pixels)
        self.assertEqual(out.raw[pixels:pixels*5//4],bytes([u])*(pixels//4))
        self.assertEqual(out.raw[pixels*5//4:pixels*3//2],bytes([v])*(pixels//4))
        self.assertEqual(out.raw[pixels*3//2:],bytes(16))
        self.assertEqual(self.convert(raw,len(raw)-1,tables,scratch,out,pixels*3//2),-1)
        self.assertEqual(self.convert(raw,len(raw),tables,scratch,out,pixels*3//2-1),-1)
