"""Private-pipe validation and CPU-default selection; no GPU or camera access."""
import ctypes
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
HEADER=ROOT/'probes/mt8163-gpu/bayer10_trial_protocol.h'


class GpuTrialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        wrapper=Path(cls.tmp.name)/'protocol.c'
        wrapper.write_text('#define _DARWIN_C_SOURCE\n#define _POSIX_C_SOURCE 200809L\n'
            f'#include "{HEADER}"\n'
            'int selected(const char*p){return trial_gpu_selected(p);}\n'
            'int request(const unsigned char*p,unsigned n){return trial_request_valid(p,n);}\n'
            'int response(const unsigned char*p,unsigned n){return trial_response_valid(p,n);}\n'
            'int receive(int fd,void*p,size_t n){trial_nonblock(fd);return trial_transfer(fd,p,n,0,trial_now_us()+20000);}\n')
        output=Path(cls.tmp.name)/'protocol.so'
        subprocess.run(['cc','-std=c11','-shared','-fPIC','-Wall','-Wextra','-Werror',
                        str(wrapper),'-o',str(output)],check=True)
        cls.lib=ctypes.CDLL(str(output))
        cls.lib.selected.argtypes=[ctypes.c_char_p]
        cls.lib.request.argtypes=[ctypes.c_void_p,ctypes.c_uint]
        cls.lib.response.argtypes=[ctypes.c_void_p,ctypes.c_uint]
        cls.lib.receive.argtypes=[ctypes.c_int,ctypes.c_void_p,ctypes.c_size_t]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_selection_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            mode=Path(directory)/'mode'
            selected=lambda:self.lib.selected(os.fsencode(mode))
            self.assertEqual(selected(),0)
            for value in [b'cpu\n',b'gpu',b'gpu\nextra',b'GPU\n',b'\0gpu']:
                mode.write_bytes(value);mode.chmod(0o600)
                self.assertEqual(selected(),0)
            mode.write_bytes(b'gpu\n');mode.chmod(0o600)
            self.assertEqual(selected(),1)
            mode.chmod(0o644);self.assertEqual(selected(),0)
            mode.chmod(0o600);link=Path(directory)/'link';link.symlink_to(mode)
            self.assertEqual(self.lib.selected(os.fsencode(link)),0)
            fifo=Path(directory)/'fifo';os.mkfifo(fifo,0o600)
            self.assertEqual(self.lib.selected(os.fsencode(fifo)),0)

    def test_exact_header_lengths_sequences_and_bounds(self):
        request=struct.pack('<4sIII',b'TGB1',2406144,7,0)
        response=struct.pack('<4sIIIII',b'TGR1',460800,7,36000,7000,0)
        self.assertEqual(self.lib.request(request,7),1)
        self.assertEqual(self.lib.response(response,7),1)
        self.assertEqual(self.lib.request(request,8),0)
        self.assertEqual(self.lib.response(response,8),0)
        for offset in [0,4,8,12]:
            value=bytearray(request);value[offset]^=1
            self.assertEqual(self.lib.request(bytes(value),7),0)
        for offset in [0,4,8,20]:
            value=bytearray(response);value[offset]^=1
            self.assertEqual(self.lib.response(bytes(value),7),0)
        self.assertEqual(self.lib.response(struct.pack('<4sIIIII',b'TGR1',460800,7,2000001,0,0),7),0)

    def test_truncation_timeout_and_clean_eof(self):
        output=ctypes.create_string_buffer(8)
        for content,expected in [(b'',0),(b'abc',-1),(b'abcdefgh',1)]:
            read,write=os.pipe()
            os.write(write,content);os.close(write)
            try:self.assertEqual(self.lib.receive(read,output,8),expected)
            finally:os.close(read)
        read,write=os.pipe();started=time.monotonic()
        try:self.assertEqual(self.lib.receive(read,output,8),-1)
        finally:os.close(read);os.close(write)
        self.assertLess(time.monotonic()-started,.5)

    def test_runtime_separation_and_fallback(self):
        wrapper=(ROOT/'probes/mt8163-vcodec/cronos_gpu_venc_trial_android.c').read_text()
        self.assertIn('PR_SET_PDEATHSIG,SIGKILL',wrapper)
        self.assertIn('for(int fd=3;fd<fd_limit;++fd)close(fd)',wrapper)
        self.assertIn('LD_LIBRARY_PATH=/data/tater-gpu/lib',wrapper)
        self.assertIn('gpu_failed=1;fallback=1',wrapper)
        self.assertIn('cronos_bayer10_balanced_yuv420(raw,bytes,luts,scratch,output,output_bytes)',wrapper)
        stream=(ROOT/'probes/mt8163-gpu/bayer10_stream_trial_android.c').read_text()
        self.assertLess(stream.index('cronos_gpu_target_allowed()'),stream.index('initialize(&g)'))
        self.assertIn('sequence<18000',stream)
        self.assertIn('alarm(2)',stream)
        for source in [wrapper,stream,HEADER.read_text()]:
            self.assertNotIn('/dev/camera',source)
            self.assertNotIn('ioctl(',source)


if __name__=='__main__':unittest.main()
