import unittest
from pathlib import Path


class CheckersGPUProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        root = Path(__file__).resolve().parents[1] / "probes" / "mt8163-gpu"
        cls.source = (root / "gles2_pbuffer_probe_android.c").read_text(
            encoding="utf-8"
        )
        cls.rotate = (root / "rgba_rotate_stream_android.c").read_text(
            encoding="utf-8"
        )
        cls.makefile = (root / "Makefile").read_text(encoding="utf-8")
        cls.readme = (root / "README.md").read_text(encoding="utf-8")
        cls.compat = (root / "bionic28_compat.c").read_text(encoding="utf-8")
        cls.optional = (root / "gpu_optional_shims.c").read_text(
            encoding="utf-8"
        )
        cls.neutralizer = (root / "neutralize_libc_o.py").read_text(
            encoding="utf-8"
        )

    def test_probe_is_offscreen_and_target_gated(self) -> None:
        self.assertIn("--offscreen-gles2-pbuffer", self.source)
        self.assertIn("--acknowledge-gpu-job", self.source)
        self.assertIn("private-pbuffer-no-display", self.source)
        self.assertNotIn("/dev/fb0", self.source)
        self.assertNotIn("/dev/mtk_disp_mgr", self.source)

    def test_probe_requires_completed_pixel_readback(self) -> None:
        self.assertIn('"eglCreatePbufferSurface"', self.source)
        self.assertIn('"glFinish"', self.source)
        self.assertIn('"glReadPixels"', self.source)
        self.assertIn("matching_pixels != WIDTH * HEIGHT", self.source)
        self.assertIn('"display_touched\\\":false', self.source)

    def test_api28_runtime_is_reproducible_without_committed_blobs(self) -> None:
        self.assertIn("android-api28", self.makefile)
        self.assertIn("ANDROID_API28_CC", self.makefile)
        self.assertIn("libbionic28compat.so", self.makefile)
        self.assertIn("LD_PRELOAD=/tmp/android-gpu28/lib/libbionic28compat.so", self.readme)
        self.assertIn("platform/prebuilts/vndk/v28", self.readme)

    def test_compatibility_layer_is_narrow_and_offscreen_only(self) -> None:
        self.assertIn("strtof_l", self.compat)
        self.assertIn("__system_property_read_callback", self.compat)
        self.assertIn("GpuAuxIsSupportFormat", self.optional)
        self.assertIn("return 0", self.optional)
        self.assertIn(".gnu.version", self.neutralizer)
        self.assertIn('version != 4', self.neutralizer)
        self.assertNotIn("/dev/fb0", self.optional)

    def test_rotate_stream_is_bounded_target_gated_and_offscreen(self) -> None:
        self.assertIn("--stream-rgba-rotate-v1", self.rotate)
        self.assertIn("--self-test-rotate", self.rotate)
        self.assertIn("--benchmark-rotate", self.rotate)
        self.assertIn("pbuffer-rotate-readback", self.rotate)
        self.assertIn("TGI1", self.rotate)
        self.assertIn("TGO1", self.rotate)
        self.assertIn("TGF1", self.rotate)
        self.assertIn("SOURCE_BYTES", self.rotate)
        self.assertIn("PANEL_BYTES", self.rotate)
        self.assertNotIn("/dev/fb0", self.rotate)
        self.assertNotIn("/dev/mtk_disp_mgr", self.rotate)

    def test_rotate_stream_uses_mali_for_rotation_and_panel_swizzle(self) -> None:
        self.assertIn('"glTexSubImage2D"', self.rotate)
        self.assertIn('"glDrawArrays"', self.rotate)
        self.assertIn('"glFinish"', self.rotate)
        self.assertIn('"glReadPixels"', self.rotate)
        self.assertIn("vec4 ink;", self.rotate)
        self.assertIn("else{ink=texture2D(frame,uv);}", self.rotate)
        self.assertIn("video_crop", self.rotate)
        self.assertIn("TGV4", self.rotate)
        self.assertIn('"scene_version\\\":4', self.rotate)
        self.assertIn("vec4 target=ink.bgra", self.rotate)
        self.assertIn("matches == PANEL_WIDTH * PANEL_HEIGHT", self.rotate)
        self.assertIn('"benchmark\\\":\\\"upload-draw-readback', self.rotate)
        self.assertIn("clock_gettime(CLOCK_MONOTONIC", self.rotate)


if __name__ == "__main__":
    unittest.main()
