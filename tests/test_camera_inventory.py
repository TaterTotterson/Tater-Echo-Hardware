import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "probes" / "mt8163-camera" / "checkers_inventory.py"


def fixture_tree(tmp_path: Path) -> tuple[Path, Path]:
    kernel = tmp_path / "kernel"
    source = (
        kernel
        / "drivers/misc/mediatek/imgsensor/src/mt8163/ov9734_mipi_raw"
        / "ov9734mipiraw_Sensor.c"
    )
    source.parent.mkdir(parents=True)
    source.write_text(
        """
        .pclk = 36000000,
        .grabwindow_width = 1280,
        .grabwindow_height = 720,
        .max_framerate = 300,
        .sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_B,
        .mclk = 24,
        .mipi_lane_num = SENSOR_MIPI_1_LANE,
        .i2c_addr_table = {0x6c, 0x20, 0xff},
        """
    )
    dts = kernel / "arch/arm64/boot/dts/mediatek"
    dts.mkdir(parents=True)
    (dts / "checkers.dtsi").write_text(
        'kd_camera_hw1@3c { compatible = "mediatek,camera_main"; reg = <0x2e>; };'
    )
    for variant in ("dvt", "evt", "hvt", "proto", "pvt"):
        (dts / f"checkers_{variant}.dts").write_text("/dts-v1/;")
    config = tmp_path / "checkers.config"
    config.write_text(
        'CONFIG_ARCH_MTK_PROJECT="checkers"\n'
        'CONFIG_CUSTOM_KERNEL_IMGSENSOR="ov9734_mipi_raw"\n'
    )
    return kernel, config


class CameraInventoryTests(unittest.TestCase):
    def test_camera_inventory_is_read_only_and_machine_readable(self):
        with tempfile.TemporaryDirectory() as directory:
            kernel, config = fixture_tree(Path(directory))
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(kernel), str(config)],
                check=True,
                capture_output=True,
                text=True,
            )
            payload = json.loads(result.stdout)
            self.assertEqual(payload["sensor"], "OV9734")
            self.assertEqual(
                payload["frame"], {"height": 720, "max_fps": 30.0, "width": 1280}
            )
            self.assertEqual(payload["mipi_lanes"], 1)
            self.assertFalse(payload["kernel_abi"]["v4l2_video_node"])
            self.assertFalse(payload["camera_powered"])
            self.assertFalse(payload["capture_performed"])

    def test_camera_inventory_rejects_another_project(self):
        with tempfile.TemporaryDirectory() as directory:
            kernel, config = fixture_tree(Path(directory))
            config.write_text(
                'CONFIG_ARCH_MTK_PROJECT="rook"\n'
                'CONFIG_CUSTOM_KERNEL_IMGSENSOR="ov9734_mipi_raw"\n'
            )
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(kernel), str(config)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("refusing non-Checkers project", result.stderr)


if __name__ == "__main__":
    unittest.main()
