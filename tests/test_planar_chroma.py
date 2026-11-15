"""Bounded OMX input adapter: swap only chroma, never change I420 producers."""
import subprocess
import tempfile
from pathlib import Path
import unittest


class PlanarChromaTests(unittest.TestCase):
    def test_plane_order_and_bounds(self):
        include = Path(__file__).resolve().parents[1] / "probes/mt8163-vcodec"
        source = r'''
#include "planar_chroma.h"
#include <assert.h>
#include <string.h>
int main(void) {
    uint8_t pixels[28], original[28];
    memset(pixels, 16, 16);
    memset(pixels + 16, 42, 4);
    memset(pixels + 20, 211, 4);
    memset(pixels + 24, 99, 4);
    memcpy(original, pixels, sizeof(pixels));
    assert(tater_i420_to_yv12(pixels, 4, 4, 23) == -1);
    assert(tater_i420_to_yv12(pixels, 3, 4, 28) == -1);
    assert(tater_i420_to_yv12(NULL, 4, 4, 28) == -1);
    assert(!memcmp(pixels, original, sizeof(pixels)));
    assert(!tater_i420_to_yv12(pixels, 4, 4, 24));
    for (unsigned n=0; n<28; ++n)
        assert(pixels[n] == (n<16 ? 16 : n<20 ? 211 : n<24 ? 42 : 99));
    assert(!tater_i420_to_yv12(pixels, 4, 4, 24));
    assert(!memcmp(pixels, original, sizeof(pixels)));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "test"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(include),
                            "-x", "c", "-", "-o", str(binary)], input=source.encode(), check=True)
            subprocess.run([str(binary)], check=True)
