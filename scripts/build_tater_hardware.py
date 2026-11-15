#!/usr/bin/env python3
"""Clean, locked Checkers helper build. Never packages owner-local libraries.

The consuming firmware supplies the source/input/output lock. All build output
is made in a new temporary tree, not in the research probe directories.
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


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        value = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def ordinary(path: Path) -> None:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"expected an ordinary input file: {path}")


def verify_sources(source: Path, lock: dict) -> None:
    for relative, expected in lock["sources"].items():
        path = source / relative
        if Path(relative).is_absolute() or ".." in Path(relative).parts:
            raise ValueError("unsafe source lock path")
        ordinary(path)
        if digest(path) != expected:
            raise ValueError(f"research source differs from the firmware lock: {relative}")


def build(lock: dict, ndk: Path, headers: Path, libraries: Path, output: Path) -> None:
    target = lock.get("target")
    if lock.get("schema") != 1 or target not in ("checkers", "cronos", "crown", "rook"):
        raise ValueError("only qualified target-owned helper locks are supported")
    profile = lock.get("codec_profile", "legacy")
    if profile not in ("legacy", "checkers-lineage-1") or (profile != "legacy" and target != "checkers"):
        raise ValueError("codec profile is not valid for this target")
    if profile == "checkers-lineage-1" and lock["link_libraries"]:
        raise ValueError("the retained-Lineage Checkers profile takes no private link libraries")
    if output.exists() or output.is_symlink():
        raise ValueError("output exists; refusing to replace it")
    verify_sources(ROOT, lock)
    properties = ndk / "source.properties"
    ordinary(properties)
    if digest(properties) != lock["ndk_properties_sha256"]:
        raise ValueError("NDK is not the pinned r21e toolchain")
    host = {"Darwin": "darwin-x86_64", "Linux": "linux-x86_64"}.get(platform.system())
    if host is None:
        raise ValueError("build on macOS or Linux")
    compiler = ndk / "toolchains/llvm/prebuilt" / host / "bin"
    for name, commit in lock["headers"].items():
        checkout = headers / name
        actual = subprocess.check_output(["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
        # Ignore build products, but never accept modified public headers.
        dirty = subprocess.check_output(["git", "-C", str(checkout), "status", "--porcelain", "--", "include", "libs"], text=True)
        if actual != commit or dirty:
            raise ValueError(f"public header checkout is not clean and pinned: {name}")
    for name, expected in lock["link_libraries"].items():
        ordinary(libraries / name)
        if digest(libraries / name) != expected:
            raise ValueError(f"owner-local link input differs from the lock: {name}")
    native = headers / "android_frameworks_native"
    core = headers / "android_system_core"
    hardware = headers / "android_hardware_libhardware"
    # Make recipes expand these flags as words: whitespace in input paths is
    # deliberately rejected rather than allowing shell metacharacters through.
    for path in (compiler, native, core, hardware, libraries):
        if any(character not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-." for character in str(path.resolve())):
            raise ValueError("build input paths must contain only letters, digits, /, _, -, and .")
    environment = {key: value for key, value in os.environ.items() if not key.startswith(("ANDROID_", "OMX_", "MAKEFLAGS", "MFLAGS"))}
    environment.update({
        "ANDROID_CC": str(compiler / "armv7a-linux-androideabi21-clang"),
        "ANDROID_CXX": str(compiler / "armv7a-linux-androideabi21-clang++"),
        "ANDROID_CFLAGS": "-O2 -Wall -Wextra -Werror -std=c11",
        "ANDROID_CXXFLAGS": "-O2 -Wall -Wextra -Werror -Wno-unused-parameter -std=c++14 -fno-exceptions -fno-rtti",
        "OMX_CFLAGS": f"-I{native}/include -I{native}/include/media/openmax",
        "ANDROID_NATIVE_CFLAGS": f"-I{hardware}/include -I{core}/include -I{core}/libsync/include",
        "ANDROID_STOCK_LIB_DIR": str(libraries),
        "ANDROID_TATER_VDEC_LINKER": "/data/tater-vdec/linker",
        "ANDROID_LDFLAGS": "",
        "SOURCE_DATE_EPOCH": "0",
    })
    if target == "cronos":
        environment["ANDROID_CFLAGS"] += " -DTATER_GPU_CRONOS"
    elif target == "crown":
        environment["ANDROID_CFLAGS"] += " -DTATER_GPU_CROWN"
    elif target == "rook":
        environment["ANDROID_CFLAGS"] += " -DTATER_GPU_ROOK"
    with tempfile.TemporaryDirectory(prefix="tater-hardware-build-") as temporary:
        tree = Path(temporary)
        for relative in lock["sources"]:
            if not relative.startswith("probes/"):
                continue
            destination = tree / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, destination)
        subprocess.run(["make", "-C", str(tree / "probes/mt8163-gpu"), "build/api28/tater-gpu-rotate"], env=environment, check=True)
        if profile == "checkers-lineage-1":
            subprocess.run(["make", "-C", str(tree / "probes/checkers-lineage-codec"), "all"], env=environment, check=True)
        else:
            codec_target = target if target in ("cronos", "crown", "rook") else "tater"
            subprocess.run(["make", "-C", str(tree / "probes/mt8163-vcodec"),
                            "android-"+codec_target+"-vdec", "android-"+codec_target+"-venc"], env=environment, check=True)
        products = {
            "tater-hw-gpu": tree / "probes/mt8163-gpu/build/api28/tater-gpu-rotate",
            "tater-hw-vdec": tree / (f"probes/mt8163-vcodec/build/tater-{target}-vdec" if target in ("cronos", "crown", "rook") else "probes/mt8163-vcodec/build/tater-vdec"),
            "tater-hw-venc": tree / (f"probes/mt8163-vcodec/build/tater-{target}-venc" if target in ("cronos", "crown", "rook") else "probes/mt8163-vcodec/build/tater-venc"),
        }
        if profile == "checkers-lineage-1":
            products.update({name: tree / "probes/checkers-lineage-codec/build" / name
                             for name in ("tater-hw-vdec", "tater-hw-venc")})
        for name, path in products.items():
            if digest(path) != lock["helpers"][name]:
                raise ValueError(f"rebuilt {name} differs from the hardware-qualified binary")
        output.parent.mkdir(parents=True, exist_ok=True)
        # No proprietary input is ever copied to output.
        output.mkdir()
        for name, path in products.items():
            shutil.copyfile(path, output / name)
            (output / name).chmod(0o755)
        manifest = {"schema": 1, "target": target, "bundle_id": lock["bundle_id"],
                    "files": lock["helpers"], "source_lock_sha256": hashlib.sha256(json.dumps(lock, sort_keys=True, separators=(",", ":")).encode()).hexdigest()}
        (output / "tater-hardware.json").write_text(json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", required=True, type=Path)
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--headers", required=True, type=Path)
    parser.add_argument("--link-libraries", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        build(json.loads(arguments.lock.read_text()), arguments.ndk.resolve(), arguments.headers.resolve(), arguments.link_libraries.resolve(), arguments.output)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"refusing: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
