#!/usr/bin/env python3
"""Prepare versioned, owner-local Checkers Lineage libraries for a future trial.

Offline only. No installation, device IO or replacement of old /data runtimes.
The public release/factory path does not consume this candidate automatically.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.build_checkers_lineage import DEFAULT_LOCK, digest, validate_lock  # noqa: E402
from scripts.prepare_tater_hardware_runtime import private_output  # noqa: E402
from scripts import prepare_tater_vdec_runtime as vdec  # noqa: E402
from scripts import prepare_tater_venc_runtime as venc  # noqa: E402

TREE = ("8564d383faba23c32545037f62a7ab9ddddf227a61790470dbc883e6eb871f0a", 66, 41_926_684)
COMPONENT = "161020e59faad3b1d250dcc3db168ce9109c08c8b2f02bc736109356f7ed5db4"
DECODER_ID = "checkers-mt8163-vdec-lineage-1"
ENCODER_ID = "checkers-mt8163-venc-lineage-1"


def prepare(source: Path, helpers: Path, output: Path) -> dict:
    private_output(output)
    lock = json.loads(DEFAULT_LOCK.read_text())
    validate_lock(lock)
    if source.is_symlink() or (source / "lib").is_symlink() or vdec.runtime_tree_identity(source) != TREE:
        raise ValueError("expected the exact shared retained-Lineage library tree")
    for name, expected in lock["helpers"].items():
        if digest(helpers / name) != expected:
            raise ValueError("candidate helper differs from lock")
    component = source / "lib/libMtkOmxVenc.so"
    if digest(component) != COMPONENT:
        raise ValueError("retained-Lineage encoder component differs from lock")
    output.mkdir(parents=True, mode=0o700)
    vdec.prepare_runtime(source, helpers / "tater-hw-vdec", output / "tater-vdec-lineage-1",
                         expected_tree_sha256=TREE[0], expected_file_count=TREE[1],
                         expected_helper_sha256=lock["helpers"]["tater-hw-vdec"],
                         include_helper=False, runtime_id=DECODER_ID)
    venc.prepare(helpers / "tater-hw-venc", component, output / "tater-venc-lineage-1",
                 helper_hash=lock["helpers"]["tater-hw-venc"], component_hash=COMPONENT,
                 include_helper=False, runtime_id=ENCODER_ID, base_tree=TREE[0])
    receipt = {"schema": 1, "target": "checkers", "private": True,
               "release_qualified": False, "installation_performed": False,
               "bundle_id": lock["bundle_id"],
               "files": {p.relative_to(output).as_posix(): digest(p)
                         for p in sorted(output.rglob("*")) if p.is_file()}}
    (output / "candidate-runtime.json").write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n")
    (output / "candidate-runtime.json").chmod(0o600)
    return {k: v for k, v in receipt.items() if k != "files"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "helpers", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--acknowledge", required=True, choices=["owner-local-runtime"])
    args = parser.parse_args()
    try:
        result = prepare(args.source.absolute(), args.helpers.absolute(), args.output.absolute())
    except (OSError, ValueError) as error:
        parser.exit(1, f"refusing: {error}\n")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
