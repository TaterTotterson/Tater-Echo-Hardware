"""Offline helper safety contracts; never substitutes for hardware pixel tests."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]/'probes/mt8183-codec'

class AthenaCodecHelperTests(unittest.TestCase):
    def test_streams_remain_bounded_without_global_frame_or_session_cap(self):
        io=(ROOT/'ap_stream_io.h').read_text()
        self.assertIn('writing?2000:10000',io)
        self.assertIn('remaining=deadline-',io)
        self.assertIn('setitimer(ITIMER_REAL',io)
        for name in ('athena_vdec.c','athena_venc.c'):
            text=(ROOT/name).read_text()
            self.assertIn('O_NONBLOCK',text)
            self.assertIn('watchdog(5)',text)
            self.assertIn('watchdog(0)',text)
            self.assertIn('fflush(stdout)',text)
            self.assertNotIn('alarm(10)',text)
            self.assertNotIn('record<304',text)
            self.assertNotIn('frames=300',text)
    def test_syscall_fence_retains_fixed_ioctl_and_watchdog_scope(self):
        text=(ROOT/'ap_sandbox.h').read_text()
        for value in ('AUDIT_ARCH_ARM','SECCOMP_RET_KILL','AP_ALLOC','AP_WAIT','AP_DCM',
                      'PR_SET_NO_NEW_PRIVS','__NR_setitimer','No writable opens'):
            self.assertIn(value,text)
        self.assertNotIn('ALLOW(__NR_ioctl)',text)
        self.assertNotIn('ALLOW(__NR_setitimer)',text)
    def test_decoder_declares_narrow_supported_subset(self):
        text=(ROOT/'athena_vdec.c').read_text()
        for value in ('baseline','single_slice_only','live_only','1280','960',
                      'stream_pool.token[slot]','stream_pool.pts[slot]',
                      'free_input!=&ring','stream_pool.displayed[free_slot]'):
            self.assertIn(value,text)
    def test_encoder_requires_actual_callback_ownership(self):
        text=(ROOT/'athena_venc.c').read_text()
        for value in ('state->output_available[slot]!=1','s->input_available[slot]',
                      'OMX_VIDEO_AVCProfileBaseline','avc.nBFrames=0','OMX_BUFFERFLAG_EOS',
                      'OMX_IndexConfigVideoIntraVOPRefresh'):
            self.assertIn(value,text)
    def test_no_private_owner_paths_or_embedded_runtime(self):
        for name in ('ap_sandbox.h','ap_legacy.c','ap_decoder_abi.h','athena_vdec.c','athena_venc.c'):
            text=(ROOT/name).read_text()
            for forbidden in ('/Users/','/Volumes/','/tmp/tater-athena','G6G1','stock-codec-reference/'):
                self.assertNotIn(forbidden,text)

if __name__=='__main__':unittest.main()
