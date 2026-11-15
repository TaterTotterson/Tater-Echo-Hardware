#!/usr/bin/env python3
"""Recreate private MT8163 libraries from retained vendor + public inputs.

This is an offline assembler, not an installer or downloader. No stock-device
backup or previously prepared development runtime is accepted as an input.
Vendor libraries stay local; use the SDK/VNDK inputs under their own licenses.
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
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.build_checkers_lineage import digest  # noqa: E402
from scripts.prepare_tater_hardware_runtime import private_output  # noqa: E402
from scripts.prepare_tater_vdec_runtime import runtime_tree_identity  # noqa: E402

LOCK_PATH = ROOT / "platforms/mt8163-lineage-runtime-sources.json"


def relative_path(value: str) -> Path:
    path = Path(value)
    if not value or path.is_absolute() or any(part in (".", "..") for part in value.split("/")):
        raise ValueError("unsafe runtime source path")
    return path


def ordinary_source(root: Path, relative: str, expected: str) -> Path:
    path = root / relative_path(relative)
    if root.is_symlink() or any(parent.is_symlink() for parent in path.parents):
        raise ValueError("runtime input must not traverse symlinks")
    if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= 32 * 1024 * 1024:
        raise ValueError("runtime input must be a bounded ordinary file")
    if digest(path) != expected:
        raise ValueError(f"runtime input differs from its source lock: {relative}")
    return path


def validate_inputs(lock: dict, roots: dict[str, Path]) -> None:
    if lock.get("schema") != 1 or set(lock["runtimes"]) != {"gpu", "codec"}:
        raise ValueError("unsupported runtime source inventory")
    for relative, expected in lock["sources"].items():
        ordinary_source(ROOT, relative, expected)
    for runtime in lock["runtimes"].values():
        for destination, entry in runtime["files"].items():
            relative_path(destination)
            if destination != "linker" and not destination.startswith("lib/"):
                raise ValueError("unexpected runtime output path")
            kind = entry["kind"]
            if kind == "shims":
                relative_path(entry["path"])
                continue
            if kind not in roots or entry.get("transform", "") not in ("", "neutralize-libc-o"):
                raise ValueError("unsupported runtime input type/transform")
            ordinary_source(roots[kind], entry["path"], entry.get("input_sha256", entry["sha256"]))


def assemble(vendor: Path, sdk: Path, vndk: Path, ndk: Path, output: Path) -> dict:
    private_output(output)
    lock = json.loads(LOCK_PATH.read_text())
    roots = {"vendor": vendor, "sdk24": sdk, "vndk28": vndk}
    validate_inputs(lock, roots)
    ordinary_source(ndk, "source.properties", lock["ndk_properties_sha256"])
    host = {"Darwin": "darwin-x86_64", "Linux": "linux-x86_64"}.get(platform.system())
    if not host:
        raise ValueError("build compatibility libraries on macOS or Linux")
    compiler = ndk / "toolchains/llvm/prebuilt" / host / "bin"
    for path in (compiler, vendor):
        if any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-." for c in str(path.resolve())):
            raise ValueError("compiler/vendor paths must not contain shell metacharacters or whitespace")
    environment = {k: v for k, v in os.environ.items() if not k.startswith(("ANDROID_", "OMX_", "MAKEFLAGS", "MFLAGS"))}
    cc = str(compiler / "armv7a-linux-androideabi21-clang")
    environment.update({"ANDROID_CC": cc,
                        "ANDROID_API28_CC": str(compiler / "armv7a-linux-androideabi28-clang"),
                        "SOURCE_DATE_EPOCH": "0"})
    with tempfile.TemporaryDirectory(prefix="tater-private-lineage-") as temporary:
        work = Path(temporary).resolve()
        for relative in lock["sources"]:
            destination = work / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, destination)
        probe = work / "probes/mt8163-gpu"
        shim_names = ("libbionic28compat.so", "libged.so", "libgpu_aux.so",
                      "libgralloc_extra.so", "libnativewindow.so", "libvndksupport.so")
        subprocess.run(["make", "-C", str(probe), *["build/api28/" + n for n in shim_names]], env=environment, check=True)
        shims = probe / "build/api28"
        common = [cc, "-shared", "-fPIC", "-Os", "-Wall", "-Wextra", "-Werror"]
        subprocess.run([*common, "-Wl,-soname,libion_ulit.so", "-o", str(shims / "libion_ulit.so"),
                        str(work / "probes/mt8163-vcodec/cronos_ion_diagnostics.c")], env=environment, check=True)
        subprocess.run([*common, "-Wl,-soname,libGLESv1_CM.so", "-Wl,--allow-shlib-undefined",
                        "-o", str(shims / "libGLESv1_CM.so"), str(probe / "gles_dependency_alias.c"),
                        "-L" + str(vendor / "egl"), "-Wl,--no-as-needed", "-lGLES_mali"], env=environment, check=True)
        roots["shims"] = shims
        # Build/verify all bytes before creating the final private output.
        for name, runtime in lock["runtimes"].items():
            tree = work / name
            for relative, entry in runtime["files"].items():
                source = ordinary_source(roots[entry["kind"]], entry["path"], entry.get("input_sha256", entry["sha256"]))
                destination = tree / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
                if entry.get("transform"):
                    subprocess.run([sys.executable, str(probe / "neutralize_libc_o.py"), str(destination)], env=environment, check=True)
                if digest(destination) != entry["sha256"]:
                    raise ValueError("generated compatibility library differs from lock")
                destination.chmod(0o500 if relative == "linker" else 0o400)
            identity = (runtime["tree_sha256"], runtime["file_count"], runtime["runtime_bytes"])
            if runtime_tree_identity(tree) != identity:
                raise ValueError("assembled runtime tree differs from lock")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.mkdir(mode=0o700)
        for name in lock["runtimes"]:
            shutil.copytree(work / name, output / name)
            (output / name).chmod(0o700)
            (output / name / "lib").chmod(0o700)
        receipt = {"schema": 1, "private": True, "installation_performed": False,
                   "source_lock_sha256": digest(LOCK_PATH),
                   "runtimes": {name: {k: v for k, v in runtime.items() if k != "files"}
                                for name, runtime in lock["runtimes"].items()}}
        (output / "source-receipt.json").write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n")
        (output / "source-receipt.json").chmod(0o600)
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("vendor", "sdk", "vndk", "ndk", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--acknowledge", required=True, choices=["owner-local-runtime"])
    args = parser.parse_args()
    try:
        result = assemble(args.vendor.absolute(), args.sdk.absolute(), args.vndk.absolute(), args.ndk.absolute(), args.output.absolute())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"refusing: {error}\n")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
