import unittest
from pathlib import Path


class CheckersRuntimeProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (
            Path(__file__).resolve().parents[1]
            / "probes"
            / "mt8163-runtime"
            / "runtime_probe.c"
        ).read_text(encoding="utf-8")

    def test_load_is_bounded_target_gated_and_anonymous(self):
        self.assertIn("--target", self.source)
        self.assertIn("--acknowledge", self.source)
        self.assertIn("bounded-load", self.source)
        self.assertIn("#define BUFFER_BYTES (32U * 1024U * 1024U)", self.source)
        self.assertIn("#define PHASE_NS UINT64_C(1000000000)", self.source)
        self.assertIn("posix_memalign", self.source)
        self.assertIn('"device_opened\\":false', self.source)
        self.assertIn('"persistent_write\\":false', self.source)
        self.assertNotIn("/dev/", self.source)
        self.assertNotIn("system(", self.source)


if __name__ == "__main__":
    unittest.main()
