"""Compile the original packet gate on the host; no runtime or device access."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AthenaBaselinePacketTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='athena-packet-test-')
        output = Path(cls.temporary.name) / 'packet.so'
        source = r'''
#include "baseline_packet.h"
int test_packet(unsigned char *data, unsigned size, unsigned *video) {
    struct ap_parameter_sets sets = {0};
    return ap_baseline_packet(&sets, data, size, video);
}
int test_change(unsigned char *a, unsigned na, unsigned char *b, unsigned nb) {
    struct ap_parameter_sets sets = {0}; unsigned video;
    if (ap_baseline_packet(&sets,a,na,&video)<0) return -2;
    return ap_baseline_packet(&sets,b,nb,&video);
}
'''
        subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-std=c11',
                        '-shared', '-fPIC', '-x', 'c', '-',
                        '-I', str(ROOT/'probes/mt8183-codec'), '-o', str(output)],
                       input=source, text=True, check=True)
        cls.library = ctypes.CDLL(str(output))
        cls.library.test_packet.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p]
        cls.library.test_change.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint]

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @staticmethod
    def nal(kind, payload):
        return b'\0\0\0\1' + bytes([kind]) + payload

    def fixture(self, slice_bits=b'\xb8'):
        return self.nal(0x67, b'\x42\xc0\x1f\x80') + self.nal(0x68, b'\x80') + self.nal(0x65, slice_bits)

    def packet(self, value):
        buffer = ctypes.create_string_buffer(value)
        video = ctypes.c_uint()
        result = self.library.test_packet(buffer, len(value), ctypes.byref(video))
        return result, buffer.raw[:video.value]

    def test_single_slice_baseline_compacts_exact_video(self):
        result, video = self.packet(self.fixture())
        self.assertEqual(result, 1)
        self.assertEqual(video, self.nal(0x65, b'\xb8'))

    def test_parameter_sets_without_picture(self):
        self.assertEqual(self.packet(self.fixture()[:-6]), (0, b''))

    def test_unsupported_profile_and_invalid_headers(self):
        for value in (b'', b'bad', self.fixture().replace(b'\x42\xc0', b'\x64\xc0'),
                      self.fixture().replace(b'\x42\xc0', b'\x42\xc1'),
                      self.nal(0x65, b'\xb8'), self.fixture()+self.nal(0x6c,b'\x80')):
            self.assertEqual(self.packet(value)[0], -1)

    def test_b_frame_multislice_nonzero_first_mb_and_truncation_rejected(self):
        for value in (self.fixture(b'\xa8'), self.fixture(b'\x4e'),
                      self.fixture(b''), self.fixture()+self.nal(0x65,b'\xb8')):
            self.assertEqual(self.packet(value)[0], -1)

    def test_parameter_changes_rejected_but_repeat_allowed(self):
        for change, expected in ((self.fixture(),1),
                                 (self.fixture().replace(b'\x1f\x80',b'\x20\x80'),-1)):
            first = ctypes.create_string_buffer(self.fixture())
            second = ctypes.create_string_buffer(change)
            self.assertEqual(self.library.test_change(first,len(self.fixture()),second,len(change)), expected)

    def test_excessive_packet_size_rejected_before_read(self):
        buffer = ctypes.create_string_buffer(self.fixture())
        video = ctypes.c_uint()
        self.assertEqual(self.library.test_packet(buffer,1024*1024,ctypes.byref(video)),-1)


if __name__ == '__main__':
    unittest.main()
