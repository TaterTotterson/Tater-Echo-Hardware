#!/usr/bin/env python3
"""Prepare the exact owner-local Checkers VDEC runtime for installation.

The source files are proprietary inputs owned by the device operator. This
tool verifies their complete tree identity and copies them only to the named
local output directory. It never downloads, publishes, or embeds those files
in the research repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path


RUNTIME_ID = "checkers-mt8163-vdec-stock-1"
EXPECTED_TREE_SHA256 = "490a3c771d1c6f979828e60b93c363ec42403ca9bd3c98b12f8c5cd0761f2cc4"
EXPECTED_FILE_COUNT = 76
EXPECTED_HELPER_SHA256 = "56091ad1146815d30be313be9e08cb671640033756de14f294f04ab04ef6ce91"


class RuntimePreparationError(ValueError):
    """Raised when an owner-local runtime does not match the reviewed set."""


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def runtime_files(source: Path) -> list[Path]:
    linker = source / "linker"
    library_dir = source / "lib"
    if not linker.is_file() or linker.is_symlink() or not library_dir.is_dir():
        raise RuntimePreparationError("runtime must contain an ordinary linker and lib directory")
    files = [linker]
    for candidate in sorted(library_dir.rglob("*")):
        if candidate.is_symlink() or not candidate.is_file():
            raise RuntimePreparationError("runtime libraries must be ordinary files")
        files.append(candidate)
    return files


def runtime_tree_identity(source: Path) -> tuple[str, int, int]:
    digest = hashlib.sha256()
    total_bytes = 0
    files = runtime_files(source)
    for path in files:
        relative = path.relative_to(source).as_posix()
        identity = file_sha256(path)
        digest.update(relative.encode("utf-8") + b"\0" + identity.encode("ascii") + b"\n")
        total_bytes += path.stat().st_size
    return digest.hexdigest(), len(files), total_bytes


def prepare_runtime(
    source: Path,
    helper: Path,
    output: Path,
    *,
    expected_tree_sha256: str = EXPECTED_TREE_SHA256,
    expected_file_count: int = EXPECTED_FILE_COUNT,
    expected_helper_sha256: str = EXPECTED_HELPER_SHA256,
    include_helper: bool = True,
    runtime_id: str = RUNTIME_ID,
) -> dict[str, object]:
    source = source.resolve()
    helper = helper.resolve()
    output = output.resolve()
    if output.exists():
        raise RuntimePreparationError("output already exists; refusing to merge or overwrite it")
    if not helper.is_file() or helper.is_symlink():
        raise RuntimePreparationError("decoder helper must be an ordinary file")
    if output == source or source in output.parents:
        raise RuntimePreparationError("output must not be inside the proprietary source tree")

    tree_sha256, file_count, total_bytes = runtime_tree_identity(source)
    helper_sha256 = file_sha256(helper)
    if tree_sha256 != expected_tree_sha256 or file_count != expected_file_count:
        raise RuntimePreparationError(
            f"runtime identity mismatch: tree={tree_sha256} files={file_count}"
        )
    if helper_sha256 != expected_helper_sha256:
        raise RuntimePreparationError(f"decoder helper identity mismatch: {helper_sha256}")

    output.mkdir(mode=0o755, parents=True)
    for path in runtime_files(source):
        relative = path.relative_to(source)
        destination = output / relative
        destination.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        destination.chmod(0o555 if relative.as_posix() == "linker" else 0o444)
    if include_helper:
        helper_destination = output / "tater-vdec"
        shutil.copyfile(helper, helper_destination)
        helper_destination.chmod(0o555)

    manifest: dict[str, object] = {
        "schema": 1 if include_helper else 2,
        "runtime_id": runtime_id,
        "tree_sha256": tree_sha256,
        "file_count": file_count,
        "runtime_bytes": total_bytes,
    }
    if include_helper:
        manifest["helper_sha256"] = helper_sha256
    manifest_path = output / "runtime.json"
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
    manifest_path.chmod(0o444)
    os.chmod(output, 0o555)
    return manifest


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--source", type=Path, required=True, help="owner-local runtime containing linker and lib/")
    result.add_argument("--helper", type=Path, required=True, help="reviewed open tater-vdec helper")
    result.add_argument("--output", type=Path, required=True, help="new local installation directory")
    result.add_argument("--acknowledge", choices=["owner-local-runtime"], required=True)
    return result


def main() -> int:
    arguments = parser().parse_args()
    try:
        manifest = prepare_runtime(arguments.source, arguments.helper, arguments.output)
    except (OSError, RuntimePreparationError) as error:
        print(f"refusing: {error}", file=sys.stderr)
        return 1
    print(json.dumps(manifest, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
