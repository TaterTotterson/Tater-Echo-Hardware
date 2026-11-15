#!/usr/bin/env python3
"""Describe whether an Android hardware tree includes an MT8163 VDEC component.

The audit is deliberately source-only. It does not inspect, load, or copy shared
objects and it does not decide whether a particular file is legally
redistributable. Its purpose is to distinguish an open OMX loader and public
ABI headers from the decoder implementation that the loader expects.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


TEXT_SUFFIXES = {
    ".bp",
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".mk",
}


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


def source_files(root: Path) -> list[Path]:
    return sorted(
        path
        for path in root.rglob("*")
        if path.is_file()
        and ".git" not in path.relative_to(root).parts
        and path.suffix.lower() in TEXT_SUFFIXES
    )


def read_sources(root: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in source_files(root):
        relative = path.relative_to(root).as_posix()
        result[relative] = path.read_text(encoding="utf-8", errors="replace")
    return result


def matching_files(sources: dict[str, str], pattern: str) -> list[str]:
    expression = re.compile(pattern, re.MULTILINE | re.DOTALL)
    return sorted(path for path, text in sources.items() if expression.search(text))


def audit(root: Path) -> dict[str, object]:
    sources = read_sources(root)
    core_path = "omx/core/src/MtkOmxCore.cpp"
    plugin_path = "omx/plugin/libstagefrighthw/MtkOMXPlugin.cpp"
    video_headers = sorted(
        path
        for path in sources
        if path.startswith("omx/videocodec/include/")
        and Path(path).suffix.lower() in {".h", ".hh", ".hpp"}
    )

    library_references = sorted(
        {
            match
            for text in sources.values()
            for match in re.findall(r"libMtkOmxVdec(?:Ex)?\.so", text)
        }
    )
    build_targets = sorted(
        {
            match
            for path, text in sources.items()
            if Path(path).name in {"Android.bp", "Android.mk"}
            for match in re.findall(r"\bname\s*:\s*\"([^\"]+)\"", text)
        }
    )
    decoder_build_targets = sorted(
        target for target in build_targets if "mtkomxvdec" in target.lower()
    )
    component_factory_definitions = matching_files(
        sources,
        r"(?:^|\n)\s*(?:extern\s+\"C\"\s*)?"
        r"[\w:<>,*&\s]+\bMtkOmxComponentCreate\s*\([^;{}]*\)\s*\{",
    )
    proprietary_include_references = sorted(
        {
            line.strip().strip('",')
            for text in sources.values()
            for line in text.splitlines()
            if "proprietary" in line.lower() and "include" in line.lower()
        }
    )

    open_core_present = core_path in sources
    plugin_present = plugin_path in sources
    factory_loaded_dynamically = any(
        re.search(
            r"dlsym\s*\([^;]*\"MtkOmxComponentCreate\"",
            text,
            re.DOTALL,
        )
        for text in sources.values()
    )
    mt8163_val_contract = any(
        "VAL_CHIP_NAME_MT8163" in sources[path] for path in video_headers
    )
    decoder_component_source_present = bool(
        decoder_build_targets or component_factory_definitions
    )

    if (
        open_core_present
        and plugin_present
        and video_headers
        and mt8163_val_contract
        and library_references
        and factory_loaded_dynamically
        and not decoder_component_source_present
    ):
        source_boundary = "open_loader_and_abi_headers_only"
    elif decoder_component_source_present:
        source_boundary = "decoder_component_source_candidate_present"
    else:
        source_boundary = "incomplete_or_unrecognized_tree"

    return {
        "repository_commit": git_commit(root),
        "source_file_count": len(sources),
        "openmax_core_source_present": open_core_present,
        "stagefright_plugin_source_present": plugin_present,
        "videocodec_header_files": video_headers,
        "mt8163_val_contract_present": mt8163_val_contract,
        "decoder_shared_object_references": library_references,
        "component_factory_loaded_dynamically": factory_loaded_dynamically,
        "decoder_build_targets": decoder_build_targets,
        "component_factory_definition_files": component_factory_definitions,
        "decoder_component_source_present": decoder_component_source_present,
        "proprietary_include_references": proprietary_include_references,
        "source_boundary": source_boundary,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tree", type=Path, help="Android hardware source tree")
    arguments = parser.parse_args()
    if not arguments.tree.is_dir():
        parser.error(f"not a directory: {arguments.tree}")
    print(json.dumps(audit(arguments.tree), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
