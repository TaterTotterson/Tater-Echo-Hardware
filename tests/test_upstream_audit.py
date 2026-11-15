import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "audit_mt8163_upstream.py"


class UpstreamAuditTests(unittest.TestCase):
    def test_reports_generic_gpu_but_missing_mt8163_platforms(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            kernel = Path(directory)
            files = {
                "drivers/gpu/drm/panfrost/panfrost_drv.c": (
                    '{ .compatible = "arm,mali-t720" },\n'
                ),
                "Documentation/devicetree/bindings/gpu/arm,mali-midgard.yaml": (
                    "- const: arm,mali-t720\n"
                ),
                "drivers/media/platform/mediatek/vcodec/decoder/mtk_vcodec_dec_drv.c": (
                    '.compatible = "mediatek,mt8173-vcodec-dec";\n'
                ),
                "arch/arm/boot/dts/mediatek/mt8127.dtsi": "// fixture\n",
            }
            for relative, contents in files.items():
                path = kernel / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")
            result = subprocess.run(
                ["python3", str(SCRIPT), str(kernel)],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(result.stdout)
            self.assertTrue(report["panfrost_generic_mali_t720"])
            self.assertTrue(report["panfrost_binding_mali_t720"])
            self.assertFalse(report["panfrost_mt8163_soc_compatible"])
            self.assertFalse(report["v4l2_vdec_mt8163"])
            self.assertEqual(report["mt8163_device_tree_files"], [])
            self.assertEqual(
                report["v4l2_vdec_compatibles"],
                ["mediatek,mt8173-vcodec-dec"],
            )
