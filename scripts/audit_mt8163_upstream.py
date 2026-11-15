#!/usr/bin/env python3
"""Report the upstream Linux support boundary relevant to Checkers/MT8163."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


def read_text(root: Path, relative: str) -> str:
    path = root / relative
    return path.read_text(encoding="utf-8") if path.is_file() else ""


def git_commit(root: Path) -> str | None:
    try:
        return subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def audit(root: Path) -> dict[str, object]:
    panfrost = read_text(root, "drivers/gpu/drm/panfrost/panfrost_drv.c")
    gpu_binding = read_text(
        root, "Documentation/devicetree/bindings/gpu/arm,mali-midgard.yaml"
    )
    vdec = read_text(
        root,
        "drivers/media/platform/mediatek/vcodec/decoder/mtk_vcodec_dec_drv.c",
    )
    dts_root = root / "arch/arm/boot/dts/mediatek"
    mt8163_dts = sorted(
        str(path.relative_to(root))
        for path in dts_root.glob("*mt8163*")
        if path.is_file()
    ) if dts_root.is_dir() else []
    vdec_compatibles = sorted(
        set(re.findall(r'"(mediatek,mt\d+-vcodec-dec)"', vdec))
    )
    return {
        "kernel_commit": git_commit(root),
        "mt8163_device_tree_files": mt8163_dts,
        "panfrost_generic_mali_t720": "arm,mali-t720" in panfrost,
        "panfrost_binding_mali_t720": "arm,mali-t720" in gpu_binding,
        "panfrost_mt8163_soc_compatible": "mediatek,mt8163-mali" in panfrost,
        "v4l2_vdec_mt8163": "mediatek,mt8163-vcodec-dec" in vdec,
        "v4l2_vdec_compatibles": vdec_compatibles,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("kernel", type=Path, help="upstream Linux source tree")
    arguments = parser.parse_args()
    if not arguments.kernel.is_dir():
        parser.error(f"not a directory: {arguments.kernel}")
    print(json.dumps(audit(arguments.kernel), sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
