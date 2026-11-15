#!/usr/bin/env python3
"""Build public compatibility DSOs; never copy/package the GLES link input.

The exact retained GLES DSO supplies the alias's dependency metadata only.
It can be extracted from the pinned Lineage installer; no device backup needed.
"""
from __future__ import annotations
import argparse
import hashlib
import gzip
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import tarfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "platforms/runtime-compat-lock.json"


def digest(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError("expected an ordinary public input")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(ndk, sync_source, gles_library, output):
    lock = json.loads(LOCK.read_text())
    if output.exists() or output.is_symlink():
        raise ValueError("output exists; refusing to overwrite it")
    for root, files in ((ROOT, lock["sources"]), (sync_source, lock["sync_sources"])):
        for relative, expected in files.items():
            if Path(relative).is_absolute() or ".." in Path(relative).parts or digest(root / relative) != expected:
                raise ValueError("public source differs from lock: " + relative)
    if digest(ndk / "source.properties") != lock["ndk_properties_sha256"]:
        raise ValueError("expected pinned NDK r21e")
    if gles_library.name != "libGLES_mali.so" or digest(gles_library) != lock["gles_link_sha256"]:
        raise ValueError("GLES alias link metadata differs from the retained Lineage DSO")
    host = {"Darwin": "darwin-x86_64", "Linux": "linux-x86_64"}[platform.system()]
    compilers = ndk.resolve() / "toolchains/llvm/prebuilt" / host / "bin"
    cc = str(compilers / "armv7a-linux-androideabi21-clang")
    cc28 = str(compilers / "armv7a-linux-androideabi28-clang")
    environment = {k: v for k, v in os.environ.items() if not k.startswith(("ANDROID_", "LD_", "DYLD_", "MAKEFLAGS", "GNUMAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "CFLAGS", "LDFLAGS", "CPATH", "C_INCLUDE_PATH", "LIBRARY_PATH"))}
    environment.update(ANDROID_CC=cc, ANDROID_API28_CC=cc28, SOURCE_DATE_EPOCH="0")
    with tempfile.TemporaryDirectory(prefix="tater-public-compat-") as temporary:
        stage = Path(temporary)
        for relative in lock["sources"]:
            destination = stage / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, destination)
        for relative in lock["sync_sources"]:
            destination = stage / "aosp-sync" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(sync_source / relative, destination)
        gpu = stage / "probes/mt8163-gpu"
        names = ["libbionic28compat.so", "libged.so", "libgpu_aux.so", "libgralloc_extra.so", "libnativewindow.so", "libvndksupport.so"]
        subprocess.run(["make", "-C", str(gpu), *["build/api28/" + n for n in names]], env=environment, check=True)
        products = gpu / "build/api28"
        common = [cc, "-shared", "-fPIC", "-Os", "-Wall", "-Wextra", "-Werror"]
        subprocess.run([*common, "-Wl,-soname,libion_ulit.so", "-o", str(products / "libion_ulit.so"), str(stage / "probes/mt8163-vcodec/cronos_ion_diagnostics.c")], env=environment, check=True)
        subprocess.run([*common, "-Wl,-soname,libGLESv1_CM.so", "-Wl,--allow-shlib-undefined", "-o", str(products / "libGLESv1_CM.so"), str(gpu / "gles_dependency_alias.c"), "-L" + str(gles_library.resolve().parent), "-Wl,--no-as-needed", "-lGLES_mali"], env=environment, check=True)
        sync = stage / "aosp-sync"
        subprocess.run([cc28, "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-I" + str(sync / "include"), "-I" + str(sync / "include/ndk"), "-Wl,-soname,libsync.so", "-Wl,--version-script," + str(sync / "libsync.map.txt"), "-o", str(products / "libsync.so"), str(sync / "sync.c")], env=environment, check=True)
        for name, expected in lock["files"].items():
            if digest(products / name) != expected:
                raise ValueError("public compatibility output differs from lock: " + name)
        output.mkdir(parents=True)
        for name in lock["files"]:
            shutil.copyfile(products / name, output / name)
        # Apache-2.0 upstream notice travels with its unmodified sync binary.
        shutil.copyfile(sync / "NOTICE", output / "AOSP-SYNC-NOTICE")
        # Provide the complete original build recipe and unmodified upstream
        # sync sources with the public binaries, never the proprietary link DSO.
        sources = {name: (ROOT / name).read_bytes() for name in lock["sources"]}
        sources.update({"aosp-sync/" + name: (sync / name).read_bytes() for name in lock["sync_sources"]})
        for name in ("LICENSE", "LICENSES/MIT.txt", "NOTICE.md", "platforms/runtime-compat-lock.json"):
            sources[name] = (ROOT / name).read_bytes()
        with (output / "runtime-compat-source.tar.gz").open("xb") as raw:
            with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                    for name, data in sorted(sources.items()):
                        entry = tarfile.TarInfo(name)
                        entry.size, entry.mode = len(data), 0o644
                        archive.addfile(entry, io.BytesIO(data))
        (output / "runtime-compat.json").write_text(json.dumps({"schema": 1, "files": lock["files"], "source_lock_sha256": digest(LOCK)}, sort_keys=True) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("ndk", "sync-source", "gles-library", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        build(args.ndk, args.sync_source, args.gles_library, args.output)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, "refusing: " + str(error) + "\n")
