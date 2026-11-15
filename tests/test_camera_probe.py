import unittest
from pathlib import Path


class CheckersCameraProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        root = Path(__file__).resolve().parents[1] / "probes" / "mt8163-camera"
        cls.source = (root / "lens_cover_query.c").read_text(encoding="utf-8")
        cls.abi_source = (root / "camera_abi_dump.c").read_text(encoding="utf-8")

    def test_lens_cover_query_is_read_only_and_target_gated(self):
        self.assertIn("--read-only-query", self.source)
        self.assertIn("--target", self.source)
        self.assertIn("O_RDONLY | O_CLOEXEC | O_NONBLOCK", self.source)
        self.assertIn("EVIOCGSW", self.source)
        self.assertIn("SW_CAMERA_LENS_COVER", self.source)
        self.assertNotIn("/dev/kd_camera_hw", self.source)
        self.assertNotIn("write(", self.source)

    def test_abi_dump_cannot_touch_hardware(self):
        self.assertIn("ISP_BUFFER_CTRL", self.abi_source)
        self.assertIn("CAM_PIPE_MGR_LOCK", self.abi_source)
        self.assertIn("SYSRAM_ALLOC", self.abi_source)
        self.assertIn("KDIMGSENSORIOC_X_FEATURECONCTROL", self.abi_source)
        self.assertIn('#include "kd_imgsensor_define.h"', self.abi_source)
        self.assertNotIn('#include "kd_imgsensor_define_checkers.h"', self.abi_source)
        self.assertIn("SENSOR_FEATURE_SET_REGISTER", self.abi_source)
        self.assertIn("SENSOR_FEATURE_GET_REGISTER", self.abi_source)
        self.assertIn("SENSOR_FEATURE_SET_TEST_PATTERN", self.abi_source)
        self.assertIn('"config_checkers\\\":false', self.abi_source)
        self.assertIn('"device_opened\\\":false', self.abi_source)
        self.assertNotIn("open(", self.abi_source)
        self.assertNotIn("ioctl(", self.abi_source)
        self.assertNotIn("mmap(", self.abi_source)
        self.assertNotIn("write(", self.abi_source)


if __name__ == "__main__":
    unittest.main()
