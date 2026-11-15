import unittest
from pathlib import Path


class CameraClockPatchTests(unittest.TestCase):
    def test_clock_handles_are_not_conditional_on_camera_power(self):
        patch = (Path(__file__).resolve().parents[1] / "kernel-patches/mt8163-camera/0001-initialize-clock-handles-without-always-on-power.patch").read_text()
        self.assertIn("+\tGet_ccf_clk(pdev);", patch)
        self.assertIn("+\t\treturn PTR_ERR(g_camclk_camtg_sel);", patch)
        self.assertIn("+\t\treturn PTR_ERR(g_camclk_univpll_d26);", patch)
        self.assertIn("+\t\treturn PTR_ERR(g_camclk_univpll2_d2);", patch)
        added = "\n".join(line[1:] for line in patch.splitlines() if line.startswith("+") and not line.startswith("+++"))
        self.assertNotIn("cmmclk-always-on", added)
        self.assertNotIn("gpio", added)
        self.assertNotIn("writel", added)
        self.assertLess(added.index("ret = clk_set_parent"), added.index("ret = clk_prepare_enable"))


if __name__ == "__main__":
    unittest.main()
