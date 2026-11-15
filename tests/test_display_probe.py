import unittest
from pathlib import Path


class CheckersDisplayProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        root = Path(__file__).resolve().parents[1] / "probes" / "mt8163-display"
        cls.source = (root / "display_query.c").read_text(encoding="utf-8")
        patch_root = (
            Path(__file__).resolve().parents[1]
            / "kernel-patches"
            / "mt8163-display"
        )
        cls.patch = (patch_root / "0001-fix-compat-suspend-query.patch").read_text(
            encoding="utf-8"
        )

    def test_query_is_read_only_and_target_gated(self) -> None:
        self.assertIn("--read-only-query", self.source)
        self.assertIn("--target", self.source)
        self.assertIn("O_RDONLY | O_CLOEXEC", self.source)
        self.assertNotIn("mmap(", self.source)
        self.assertNotIn("FBIOPAN_DISPLAY", self.source)
        self.assertNotIn("DISP_IOCTL_TRIGGER_SESSION", self.source)

    def test_query_covers_framebuffer_and_session_capabilities(self) -> None:
        self.assertIn("FBIOGET_FSCREENINFO", self.source)
        self.assertIn("FBIOGET_VSCREENINFO", self.source)
        self.assertIn("DISP_IOCTL_GET_SESSION_INFO", self.source)
        self.assertIn("DISP_IOCTL_GET_DISPLAY_CAPS", self.source)
        self.assertIn('display_touched\\\":false', self.source)

    def test_compat_suspend_fix_passes_a_pointer_to_native_ioctl(self) -> None:
        self.assertIn("compat_alloc_user_space(sizeof(*data))", self.patch)
        self.assertIn("unsigned int __user *data", self.patch)
        self.assertIn("put_user(value, data32)", self.patch)


if __name__ == "__main__":
    unittest.main()
