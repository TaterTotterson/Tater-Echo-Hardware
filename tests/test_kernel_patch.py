import unittest
from pathlib import Path


class CheckersKernelPatchTests(unittest.TestCase):
    def test_experimental_patch_is_prominently_rejected(self) -> None:
        readme = (
            Path(__file__).resolve().parents[1]
            / "kernel-patches"
            / "checkers"
            / "README.md"
        ).read_text(encoding="utf-8")

        self.assertIn("Rejected by live hardware testing", readme)
        self.assertIn("no Tater firmware should consume it", readme)

    def test_decoder_patch_carries_complete_resource_set(self) -> None:
        patch = (
            Path(__file__).resolve().parents[1]
            / "kernel-patches"
            / "checkers"
            / "0001-enable-mt8163-jpeg-decoder.patch"
        ).read_text(encoding="utf-8")

        required = (
            "+  ccflags-y += -DJPEG_DEC_DRIVER",
            "+\t\t\t      <0 0x17004000 0 0x1000>;",
            "+\t\t\t\t     <GIC_SPI 209 IRQ_TYPE_LEVEL_LOW>;",
            "+\t\t\tclocks = <&vencsys CLK_VENC_CKE2>, <&vencsys CLK_VENC_CKE3>;",
            '"venc-jpgenc", "MT_CG_VENC_JPGDEC"',
            "jpeg_drv_dec_isr, IRQF_TRIGGER_LOW",
        )
        for evidence in required:
            with self.subTest(evidence=evidence):
                self.assertIn(evidence, patch)

        self.assertNotIn("+\t\tif (request_irq(gJpegqDev.decIrqId, jpeg_drv_dec_isr, IRQF_TRIGGER_FALLING", patch)


if __name__ == "__main__":
    unittest.main()
