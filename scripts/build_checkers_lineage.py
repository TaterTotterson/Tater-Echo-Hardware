#!/usr/bin/env python3
"""Reproduce the offline Checkers retained-Lineage codec candidate.

No vendor libraries, device access, installation or release-gate changes.
Existing published helper locks/bundles are intentionally left unchanged.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCK = ROOT / "platforms/checkers-lineage-candidate-lock.json"


def digest(path: Path) -> str:
    if path.is_symlink() or not path.is_file():
        raise ValueError("input must be an ordinary file")
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def validate_lock(lock: dict, source: Path = ROOT) -> None:
    if (lock.get("schema"), lock.get("target"), lock.get("release_qualified")) != (1, "checkers", False):
        raise ValueError("expected an unqualified Checkers candidate lock")
    if set(lock["helpers"]) != {"tater-hw-vdec", "tater-hw-venc"}:
        raise ValueError("unexpected candidate executable inventory")
    for relative, expected in lock["sources"].items():
        if Path(relative).is_absolute() or ".." in Path(relative).parts:
            raise ValueError("unsafe source path")
        if digest(source / relative) != expected:
            raise ValueError(f"candidate source differs from lock: {relative}")


def build(lock: dict, ndk: Path, headers: Path, output: Path) -> None:
    validate_lock(lock)
    if output.exists() or output.is_symlink():
        raise ValueError("output exists; refusing to replace it")
    if digest(ndk / "source.properties") != lock["ndk_properties_sha256"]:
        raise ValueError("expected the pinned NDK r21e")
    host = {"Darwin": "darwin-x86_64", "Linux": "linux-x86_64"}.get(platform.system())
    if not host:
        raise ValueError("build on macOS or Linux")
    compiler = ndk / "toolchains/llvm/prebuilt" / host / "bin/armv7a-linux-androideabi21-clang"
    for name, expected in lock["headers"].items():
        checkout = headers / name
        actual = subprocess.check_output(["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
        dirty = subprocess.check_output(["git", "-C", str(checkout), "status", "--porcelain", "--", "include", "libs"], text=True)
        if actual != expected or dirty:
            raise ValueError("public OpenMAX headers are not clean and pinned")
    for path in (compiler, headers):
        if any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-." for c in str(path.resolve())):
            raise ValueError("build paths must not contain whitespace or shell metacharacters")
    environment = {k: v for k, v in os.environ.items() if not k.startswith(("ANDROID_", "OMX_", "MAKEFLAGS", "MFLAGS", "CFLAGS", "LDFLAGS"))}
    native = headers / "android_frameworks_native"
    environment.update({"ANDROID_CC": str(compiler),
                        "OMX_CFLAGS": f"-I{native}/include -I{native}/include/media/openmax",
                        "SOURCE_DATE_EPOCH": "0"})
    with tempfile.TemporaryDirectory(prefix="tater-checkers-lineage-build-") as temporary:
        tree = Path(temporary)
        for relative in lock["sources"]:
            if relative.startswith("probes/"):
                destination = tree / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / relative, destination)
        probe = tree / "probes/checkers-lineage-codec"
        subprocess.run(["make", "-C", str(probe), "all"], env=environment, check=True)
        for name, expected in lock["helpers"].items():
            if digest(probe / "build" / name) != expected:
                raise ValueError(f"candidate executable differs from lock: {name}")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.mkdir()
        for name in lock["helpers"]:
            shutil.copyfile(probe / "build" / name, output / name)
            (output / name).chmod(0o755)
        receipt = {"schema": 1, "target": "checkers", "release_qualified": False,
                   "bundle_id": lock["bundle_id"], "files": lock["helpers"],
                   "source_lock_sha256": hashlib.sha256(json.dumps(lock, sort_keys=True, separators=(",", ":")).encode()).hexdigest()}
        (output / "candidate.json").write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    for name in ("ndk", "headers", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    try:
        build(json.loads(args.lock.read_text()), args.ndk.resolve(), args.headers.resolve(), args.output)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"refusing: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
