#!/usr/bin/env python3
"""Prepare all Checkers private libraries locally, separately from firmware.

No downloads, device writes, credentials, helpers, or public-release outputs.
The output is an explicit input to the factory's provision_hardware.py tool.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import prepare_tater_gpu_runtime as gpu  # noqa: E402
from scripts import prepare_tater_vdec_runtime as vdec  # noqa: E402
from scripts import prepare_tater_venc_runtime as venc  # noqa: E402


def private_output(output: Path) -> None:
    if output.exists() or output.is_symlink():
        raise ValueError("output exists; refusing to merge or replace private runtimes")
    output = output.resolve()
    # Inside a Git worktree, only explicitly ignored output is acceptable.
    parent = output.parent
    while not parent.exists():
        parent = parent.parent
    repository = subprocess.run(["git", "-C", str(parent), "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    if repository.returncode == 0:
        root = Path(repository.stdout.strip())
        ignored = subprocess.run(["git", "-C", str(root), "check-ignore", "-q", "--no-index", str(output)])
        if ignored.returncode != 0:
            raise ValueError("private runtime output must be outside Git or explicitly ignored")
    if any(part.lower() in {"release", "releases", "dist", "factory", "payload"} for part in output.parts):
        raise ValueError("private libraries must not be written into release/factory output")


def prepare(gpu_source: Path, vdec_source: Path, component: Path, helpers: Path, output: Path, *, target: str = "checkers") -> dict:
    if target not in ("checkers", "cronos", "rook"):
        raise ValueError("unqualified private runtime target")
    private_output(output)
    gpu_helper, decoder_helper, encoder_helper = gpu.EXPECTED_HELPER_SHA256, vdec.EXPECTED_HELPER_SHA256, venc.EXPECTED_HELPER
    decoder_identity = (vdec.EXPECTED_TREE_SHA256, vdec.EXPECTED_FILE_COUNT, 12_839_369)
    encoder_component = venc.EXPECTED_COMPONENT
    if target == "cronos":
        gpu_helper = "481b74fb3a329e17039bc2012f5e616996648cf3f68a9a18956928a1d26b7ea5"
        decoder_helper = "b6f9cf50e7a05aec9cf3a0cfaaee9277d0e16d8c81eda61662c6dc0331577f2d"
        encoder_helper = "920bf4a08373566478c8913e5ab6e3cf161cce835137ae513164bada2fc52709"
        decoder_identity = ("8564d383faba23c32545037f62a7ab9ddddf227a61790470dbc883e6eb871f0a", 66, 41_926_684)
        encoder_component = "161020e59faad3b1d250dcc3db168ce9109c08c8b2f02bc736109356f7ed5db4"
    elif target == "rook":
        gpu_helper = "0b69363ff3856a13343def35e90a92f363dfcc8ac880cf21b7b36730061c84b6"
        decoder_helper = "c103fcf7ed2e492ead281ed3c86847f502417abbbc2b2e2387b8001e20046cbd"
        encoder_helper = "6dbecd9c9d673af97a2c860bc095a406ab967ae7ca0d2240f2af87fb00c56bf5"
        decoder_identity = ("8564d383faba23c32545037f62a7ab9ddddf227a61790470dbc883e6eb871f0a", 66, 41_926_684)
        encoder_component = "161020e59faad3b1d250dcc3db168ce9109c08c8b2f02bc736109356f7ed5db4"
    # Validate every input before creating any output.
    if gpu.runtime_tree_identity(gpu_source) != (gpu.EXPECTED_TREE_SHA256, gpu.EXPECTED_FILE_COUNT, gpu.EXPECTED_RUNTIME_BYTES):
        raise ValueError("GPU library tree is not qualified")
    if vdec.runtime_tree_identity(vdec_source) != decoder_identity:
        raise ValueError("decoder library tree is not qualified")
    for path, expected in ((helpers / "tater-hw-gpu", gpu_helper),
                           (helpers / "tater-hw-vdec", decoder_helper),
                           (helpers / "tater-hw-venc", encoder_helper),
                           (component, encoder_component)):
        if path.is_symlink() or not path.is_file() or gpu.file_sha256(path) != expected:
            raise ValueError("helper/component identity is not qualified")
    output.mkdir(parents=True, mode=0o700)
    gpu.prepare_runtime(gpu_source, helpers / "tater-hw-gpu", output / "tater-gpu", include_helper=False,
                        expected_helper_sha256=gpu_helper, runtime_id=target+"-mt8163-mali-pbuffer-1")
    vdec.prepare_runtime(vdec_source, helpers / "tater-hw-vdec", output / "tater-vdec", include_helper=False,
                         expected_tree_sha256=decoder_identity[0],expected_file_count=decoder_identity[1],
                         expected_helper_sha256=decoder_helper,runtime_id=target+"-mt8163-vdec-stock-1")
    venc.prepare(helpers / "tater-hw-venc", component, output / "tater-venc", include_helper=False,
                 helper_hash=encoder_helper,component_hash=encoder_component,
                 runtime_id=target+"-mt8163-venc-stock-1",base_tree=decoder_identity[0])
    manifest = {"schema": 1, "target": target, "private": True,
                "files": {path.relative_to(output).as_posix(): gpu.file_sha256(path)
                          for path in sorted(output.rglob("*")) if path.is_file()}}
    (output / "private-runtime.json").write_text(json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n")
    (output / "private-runtime.json").chmod(0o600)
    return {"target": target, "private": True, "file_count": len(manifest["files"])}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("gpu-source", "vdec-source", "venc-component", "helpers", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--acknowledge", required=True, choices=["owner-local-runtime"])
    parser.add_argument("--target", choices=["checkers", "cronos", "rook"], default="checkers")
    arguments = parser.parse_args()
    try:
        result = prepare(arguments.gpu_source, arguments.vdec_source, arguments.venc_component, arguments.helpers, arguments.output, target=arguments.target)
    except (OSError, ValueError) as error:
        parser.exit(1, f"refusing: {error}\n")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
