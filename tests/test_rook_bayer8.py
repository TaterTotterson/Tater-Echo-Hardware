"""Generated-only Rook RGGB8/YUV and bounds checks; no device access."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class RookBayerTests(unittest.TestCase):
    def test_primary_colors_planar_order_and_buffer_bounds(self):
        with tempfile.TemporaryDirectory() as temporary:
            library=Path(temporary)/"rook.so"
            wrapper='#include "rook_bayer8.h"\nint convert(const void *p,size_t n,unsigned w,unsigned h,void *o,size_t c){return rook_bayer8_yuv420(p,n,w,h,o,c);}\n'
            subprocess.run(["cc","-shared","-fPIC","-std=c11","-O2","-Wall","-Wextra","-Werror","-I",str(ROOT/"probes/mt8163-vcodec"),"-x","c","-","-o",str(library)],input=wrapper.encode(),check=True)
            converter=ctypes.CDLL(str(library)).convert
            converter.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint,ctypes.c_uint,ctypes.c_void_p,ctypes.c_size_t]
            tables=bytes(i//4 for i in range(1024))*3
            for width,height in ((640,480),(320,240)):
                size=width*height*3//2
                for red,green,blue in ((128,128,128),(255,0,0),(0,255,0),(0,0,255),(0,0,0),(255,255,255)):
                    with self.subTest(width=width,color=(red,green,blue)):
                        raw=(bytes((red,green))*320+bytes((green,blue))*320)*240
                        record=tables+raw
                        output=ctypes.create_string_buffer(size+16)
                        self.assertEqual(converter(record,len(record),width,height,output,size),0)
                        yy=16+((66*red+129*green+25*blue+128)>>8)
                        uu=128+((-38*red-74*green+112*blue+128)>>8)
                        vv=128+((112*red-94*green-18*blue+128)>>8)
                        self.assertEqual(output.raw,bytes([yy])*(width*height)+bytes([uu])*(width*height//4)+bytes([vv])*(width*height//4)+bytes(16))
                        self.assertEqual(converter(record,len(record)-1,width,height,output,size),-1)
                        self.assertEqual(converter(record,len(record),width,height,output,size-1),-1)
                self.assertEqual(converter(record,len(record),640,360,output,size),-1)
