#!/usr/bin/env python3
"""Emit a read-only Checkers camera inventory from the public kernel tree."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def require(pattern: str, text: str, label: str, flags: int = 0) -> re.Match[str]:
    match = re.search(pattern, text, flags)
    if match is None:
        raise SystemExit(f"missing expected {label}")
    return match


def config_value(config: str, name: str) -> str:
    match = require(
        rf"^{re.escape(name)}=(?:\"([^\"]*)\"|(\S+))$",
        config,
        name,
        re.MULTILINE,
    )
    return match.group(1) or match.group(2)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("kernel", type=Path, help="public MT8163 kernel root")
    parser.add_argument("config", type=Path, help="Checkers kernel configuration")
    args = parser.parse_args()

    config = args.config.read_text()
    project = config_value(config, "CONFIG_ARCH_MTK_PROJECT")
    driver = config_value(config, "CONFIG_CUSTOM_KERNEL_IMGSENSOR")
    if project != "checkers":
        raise SystemExit(f"refusing non-Checkers project: {project}")
    if driver != "ov9734_mipi_raw":
        raise SystemExit(f"unexpected Checkers camera driver: {driver}")

    relative_sensor = Path(
        "drivers/misc/mediatek/imgsensor/src/mt8163/ov9734_mipi_raw/"
        "ov9734mipiraw_Sensor.c"
    )
    sensor = (args.kernel / relative_sensor).read_text()
    dtsi = (
        args.kernel / "arch/arm64/boot/dts/mediatek/checkers.dtsi"
    ).read_text()
    variants = sorted(
        path.stem.removeprefix("checkers_")
        for path in (
            args.kernel / "arch/arm64/boot/dts/mediatek"
        ).glob("checkers_*.dts")
    )

    main_camera = require(
        r"kd_camera_hw1@[^\s{]+\s*\{.*?compatible\s*=\s*"
        r'"mediatek,camera_main";.*?reg\s*=\s*<([^>]+)>;',
        dtsi,
        "main camera device-tree node",
        re.DOTALL,
    )
    width = int(require(r"\.grabwindow_width\s*=\s*(\d+)", sensor, "width").group(1))
    height = int(require(r"\.grabwindow_height\s*=\s*(\d+)", sensor, "height").group(1))
    pclk = int(require(r"\.pclk\s*=\s*(\d+)", sensor, "pixel clock").group(1))
    fps_tenths = int(
        require(r"\.max_framerate\s*=\s*(\d+)", sensor, "frame rate").group(1)
    )
    mclk = int(require(r"\.mclk\s*=\s*(\d+)", sensor, "master clock").group(1))
    lanes = int(
        require(r"\.mipi_lane_num\s*=\s*SENSOR_MIPI_(\d+)_LANE", sensor, "lanes").group(1)
    )
    raw_order = require(
        r"\.sensor_output_dataformat\s*=\s*SENSOR_OUTPUT_FORMAT_([A-Z_]+)",
        sensor,
        "raw order",
    ).group(1)
    addresses = []
    for address in require(
        r"\.i2c_addr_table\s*=\s*\{([^}]+)\}", sensor, "I2C addresses"
    ).group(1).split(","):
        address = address.strip()
        if address != "0xff":
            addresses.append(int(address, 16))

    result = {
        "project": project,
        "sensor_driver": driver,
        "sensor": "OV9734",
        "interface": "MIPI",
        "mipi_lanes": lanes,
        "raw_order": raw_order,
        "frame": {"width": width, "height": height, "max_fps": fps_tenths / 10},
        "clocks_mhz": {"master": mclk, "pixel": pclk / 1_000_000},
        "i2c_write_addresses": addresses,
        "device_tree": {
            "main_compatible": "mediatek,camera_main",
            "main_reg": main_camera.group(1).strip(),
            "variants": variants,
        },
        "kernel_abi": {
            "sensor_devices": ["/dev/kd_camera_hw", "/dev/kd_camera_hw_bus2"],
            "isp_devices": ["/dev/camera-isp", "/dev/camera-pipemgr", "/dev/camera-sysram"],
            "v4l2_video_node": False,
            "kind": "private MediaTek character-device ABI",
        },
        "source": str(relative_sensor),
        "camera_powered": False,
        "capture_performed": False,
    }
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
