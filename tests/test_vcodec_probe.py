import unittest
from pathlib import Path


class CheckersVcodecProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        root = Path(__file__).resolve().parents[1] / "probes" / "mt8163-vcodec"
        cls.lock = (root / "lock_probe.c").read_text(encoding="utf-8")
        cls.capability = (root / "capability_probe_android.c").read_text(
            encoding="utf-8"
        )
        cls.lifecycle = (root / "lifecycle_probe_android.c").read_text(
            encoding="utf-8"
        )
        cls.mva = (root / "mva_probe_android.c").read_text(encoding="utf-8")
        cls.init = (root / "init_probe_android.c").read_text(encoding="utf-8")
        cls.omx = (root / "omx_probe_android.c").read_text(encoding="utf-8")
        cls.omx_native = (root / "omx_native_buffer_android.cpp").read_text(
            encoding="utf-8"
        )
        cls.vdec = (root / "tater_vdec_android.c").read_text(encoding="utf-8")
        cls.vdec_header = (root / "tater_vdec_android.h").read_text(
            encoding="utf-8"
        )
        cls.vdec_main = (root / "tater_vdec_main_android.c").read_text(
            encoding="utf-8"
        )
        cls.makefile = (root / "Makefile").read_text(encoding="utf-8")
        cls.omx_capture = (root / "omx_capture_shim_android.c").read_text(
            encoding="utf-8"
        )

    def test_lock_probe_is_bounded_and_cleanup_guarded(self) -> None:
        self.assertIn("#define VAL_DRIVER_TYPE_H264_DEC 6U", self.lock)
        self.assertIn(
            "_Static_assert(sizeof(struct compat_val_hw_lock) == 32", self.lock
        )
        self.assertIn("--acknowledge-vdec-power-cycle", self.lock)
        self.assertIn("sigprocmask(SIG_BLOCK", self.lock)
        self.assertIn("ioctl(fd, VCODEC_UNLOCKHW, &request)", self.lock)

    def test_capability_probe_covers_stock_decoder_formats(self) -> None:
        self.assertIn(
            "sizeof(struct vdec_query_video_format) == 44", self.capability
        )
        self.assertIn("VDEC_QUERY_FEATURE_SUPPORTED", self.capability)
        self.assertIn("queue_input_feature", self.capability)
        for name in ("mpeg2", "mpeg4", "h263", "h264", "h265"):
            with self.subTest(name=name):
                self.assertIn(f'{{"{name}",', self.capability)

    def test_lifecycle_probe_uses_public_create_release_pair(self) -> None:
        self.assertIn('"eVDecDrvCreate"', self.lifecycle)
        self.assertIn('"eVDecDrvRelease"', self.lifecycle)
        self.assertIn("VDEC_FORMAT_H264", self.lifecycle)

    def test_mva_probe_always_has_an_unmap_path(self) -> None:
        self.assertIn('"eVideoAllocMVA"', self.mva)
        self.assertIn('"eVideoFreeMVA"', self.mva)
        self.assertIn("cleanup_mva:", self.mva)

    def test_decode_probe_pins_public_abi_and_checks_real_output(self) -> None:
        for size in (56, 40, 120):
            with self.subTest(size=size):
                self.assertIn(f"== {size},", self.init)
        self.assertIn("VDEC_INPUT_FLAG_CODEC_CONFIG", self.init)
        self.assertIn("VDEC_INPUT_FLAG_END_OF_STREAM", self.init)
        self.assertIn("decode_decoder(decoder, NULL, &frame)", self.init)
        self.assertIn("output_changed_bytes == 0", self.init)
        self.assertIn("distinct_output_values < 3", self.init)
        self.assertIn("--decode-one-h264-frame", self.init)
        self.assertIn("--probe-yuv-layout", self.init)
        self.assertIn("--dump-one-h264-rgba", self.init)
        self.assertIn("write_exact_file(argv[3]", self.init)
        self.assertIn("score_luma", self.init)
        self.assertIn("score_chroma_interleaved", self.init)
        self.assertIn("tater_mtk_yuv420_block_to_rgba8888", self.init)

    def test_stream_probe_uses_bounded_owner_local_compatibility(self) -> None:
        self.assertIn('"eVDecDrvDecodeEx"', self.init)
        self.assertIn("VDEC_SET_PRIVATE_OUTPUT_CALLBACK", self.init)
        self.assertIn(
            "sizeof(struct vdec_private_output_callback) == 12", self.init
        )
        self.assertIn("provide_stream_output", self.init)
        self.assertIn("VDEC_DECODE_MODE_LOW_LATENCY 8U", self.init)
        self.assertIn("--dump-synthetic-h264-stream-rgba", self.init)
        self.assertIn("stream_rgba_sequence_size", self.init)
        self.assertIn("write_exact_file(argv[4]", self.init)
        self.assertIn("64U * 1024U * 1024U", self.init)
        self.assertIn("input_pool_offset", self.init)
        self.assertIn("stream_lookahead_ring", self.init)
        self.assertIn(
            "unique_stream_frames != expected_stream_frames", self.init
        )
        self.assertNotIn("printf(\"%p", self.init)

    def test_rejected_raw_normal_stream_mode_is_not_exposed(self) -> None:
        self.assertNotIn("--dump-normal-h264-stream-rgba", self.init)

    def test_omx_probe_is_bounded_and_uses_public_interface(self) -> None:
        self.assertIn("--inspect-avc-component", self.omx)
        self.assertIn("--exercise-avc-buffers", self.omx)
        self.assertIn("--decode-avc-stream", self.omx)
        self.assertIn("MtkOmxComponentCreate", self.omx)
        self.assertIn("OMX_IndexParamVideoInit", self.omx)
        self.assertIn("OMX_IndexParamPortDefinition", self.omx)
        self.assertIn("MAX_VIDEO_PORTS 8U", self.omx)
        self.assertIn("OMX_CALL_TIMEOUT_SECONDS 10U", self.omx)
        self.assertIn("ComponentDeInit", self.omx)
        self.assertIn("OMX_StateExecuting", self.omx)
        self.assertIn("AllocateBuffer", self.omx)
        self.assertIn("FreeBuffer", self.omx)
        self.assertIn("OMX_BUFFERFLAG_ENDOFFRAME", self.omx)
        self.assertIn("OMX_BUFFERFLAG_EOS", self.omx)
        self.assertIn("MAX_DECODE_BYTES (64U * 1024U * 1024U)", self.omx)

    def test_native_omx_probe_detiles_direct_ion_output(self) -> None:
        self.assertIn("--decode-avc-native-stream", self.omx)
        self.assertIn("TATER_MTK_COLOR_FORMAT_NATIVE", self.omx)
        self.assertIn("tater_native_buffer_copy_mtk_yuv420p", self.omx)
        self.assertIn("tater_native_buffer_copy_mtk_rgba8888", self.omx)
        self.assertIn(
            "_ZN7android9SingletonINS_20GuiExtClientProducerEE11getInstanceEv",
            self.omx_native,
        )
        self.assertIn("gralloc.mt8163.mali.so", self.omx_native)
        self.assertIn('getenv("TATER_VDEC_GRALLOC")', self.omx_native)
        self.assertNotIn("/tmp/omx-runtime", self.omx_native)
        self.assertIn(
            "tater_mtk_yuv420_block_to_yuv420p", self.omx_native
        )
        self.assertIn("eVideoInvalidateCache", self.omx_native)
        self.assertIn("omx_native_yuv_block.o", self.makefile)
        self.assertNotIn("nanosleep", self.omx_native)
        self.assertNotIn("TATER_NATIVE_DIRECT_ION", self.omx_native)
        self.assertNotIn("printf(\"%p", self.omx_native)

    def test_tater_vdec_is_a_bounded_reusable_adapter(self) -> None:
        self.assertIn("android-tater-vdec", self.makefile)
        self.assertIn(
            "ANDROID_TATER_VDEC_LINKER ?= /data/tater-vdec/linker",
            self.makefile,
        )
        self.assertIn("-Wl,--export-dynamic", self.makefile)
        self.assertIn("tater_omx_probe_main", self.vdec)
        self.assertIn("--decode-avc-native-rgba-stream", self.vdec)
        self.assertIn("TATER_VDEC_MAX_WIDTH = 1920", self.vdec_header)
        self.assertIn("TATER_VDEC_MAX_HEIGHT = 1088", self.vdec_header)
        self.assertIn("TATER_VDEC_MAX_BATCH_FRAMES = 120", self.vdec_header)
        self.assertIn("--decode-h264-batch-rgba", self.vdec_main)
        self.assertIn("--stream-h264-rgba", self.vdec_main)
        self.assertIn("--stream-h264-rgba-v2", self.vdec_main)
        self.assertIn("--serve-avc-native-rgba-stream", self.omx)
        self.assertIn("--serve-avc-native-rgba-stream-v2", self.omx)
        self.assertIn("read_stream_packet", self.omx)
        self.assertIn("access_unit_has_vcl", self.omx)
        self.assertIn("write_stream_v2_header", self.omx)
        self.assertIn("write_stream_v2_record", self.omx)
        self.assertIn("descriptor_flags | O_NONBLOCK", self.omx)
        self.assertIn("signal(SIGPIPE, SIG_IGN)", self.omx)
        self.assertIn("poll(&ready, 1, 10000)", self.omx)
        self.assertIn("OMX fatal path is using process-exit resource cleanup", self.omx)
        self.assertIn("_exit(1)", self.omx)
        self.assertIn("stream_write_failures", self.omx)
        self.assertIn("rgba8888", self.vdec_main)
        self.assertNotIn("/dev/", self.vdec)
        self.assertNotIn("libMtkOmxVdecEx.so", self.vdec)
        self.assertNotIn("printf(\"%p", self.vdec)

    def test_omx_capture_shim_is_bounded_and_validates_public_frames(self) -> None:
        self.assertIn("eVDecDrvGetParam", self.omx_capture)
        self.assertIn("VDEC_GET_DISPLAY_FRAME_BUFFER 2U", self.omx_capture)
        self.assertIn("MAX_CAPTURE_FRAMES 32U", self.omx_capture)
        self.assertIn("MAX_CAPTURE_DIMENSION 256U", self.omx_capture)
        self.assertIn("frame->base.hardware_address", self.omx_capture)
        self.assertIn("tater_mtk_yuv420_block_to_rgba8888", self.omx_capture)
        self.assertIn("O_TRUNC", self.omx_capture)
        self.assertNotIn("printf(\"%p", self.omx_capture)
        self.assertNotIn("/dev/", self.omx)


if __name__ == "__main__":
    unittest.main()
