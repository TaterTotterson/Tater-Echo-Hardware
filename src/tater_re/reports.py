"""Deterministic offline capability and dependency reports."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .workspace import Workspace, utc_now


CAPABILITY_TERMS = {
    "video-codec": (
        "vcodec",
        "vdec",
        "venc",
        "omx",
        "stagefright",
        "mediacodec",
    ),
    "jpeg-image": ("jpeg", "jpg", "imagecodec", "dpframework"),
    "gpu-display": (
        "mali",
        "gralloc",
        "gles",
        "egl",
        "surfaceflinger",
        "display",
        "hwcomposer",
    ),
    "shared-memory": ("libion", "m4u", "mmutils", "bwc"),
    "microphone-audio": (
        "audio.primary",
        "audioflinger",
        "tinyalsa",
        "speech",
        "soundtrigger",
        "webrtc_audio",
        "audiostream",
        "audiocust",
    ),
    "camera": ("camera", "libcam", "imageio", "featureio"),
}


def _artifact_text(digest: str, artifact: dict[str, Any]) -> str:
    elf = artifact.get("metadata", {}).get("elf", {})
    values = [
        digest,
        artifact.get("kind", ""),
        elf.get("soname", ""),
        elf.get("interpreter", ""),
        *elf.get("needed", []),
        *(location.get("relative_path", "") for location in artifact.get("locations", [])),
    ]
    return "\n".join(str(value) for value in values).casefold()


def capability_report(workspace: Workspace) -> dict[str, Any]:
    groups: dict[str, list[dict[str, Any]]] = {name: [] for name in CAPABILITY_TERMS}
    for digest, artifact in workspace.records():
        text = _artifact_text(digest, artifact)
        elf = artifact.get("metadata", {}).get("elf", {})
        locations = artifact.get("locations", [])
        for name, terms in CAPABILITY_TERMS.items():
            matched = sorted(term for term in terms if term.casefold() in text)
            if not matched:
                continue
            groups[name].append(
                {
                    "sha256": digest,
                    "path": locations[0].get("relative_path", "") if locations else "",
                    "kind": artifact.get("kind", ""),
                    "architecture": elf.get("machine", ""),
                    "bits": elf.get("bits", 0),
                    "soname": elf.get("soname", ""),
                    "matched": matched,
                }
            )
    return {
        "schema_version": 1,
        "generated_at": utc_now(),
        "workspace": str(workspace.root),
        "groups": groups,
    }


def dependency_report(workspace: Workspace) -> dict[str, Any]:
    providers: dict[str, list[str]] = {}
    elf_records: list[tuple[str, dict[str, Any]]] = []
    for digest, artifact in workspace.records():
        elf = artifact.get("metadata", {}).get("elf")
        if not elf:
            continue
        elf_records.append((digest, artifact))
        names = set()
        if elf.get("soname"):
            names.add(elf["soname"])
        for location in artifact.get("locations", []):
            names.add(Path(location.get("relative_path", "")).name)
        for name in names:
            if name:
                providers.setdefault(name, []).append(digest)

    nodes = []
    unresolved = set()
    for digest, artifact in elf_records:
        elf = artifact["metadata"]["elf"]
        needed = []
        for library in elf.get("needed", []):
            matches = sorted(set(providers.get(library, [])))
            needed.append({"name": library, "providers": matches})
            if not matches:
                unresolved.add(library)
        locations = artifact.get("locations", [])
        nodes.append(
            {
                "sha256": digest,
                "path": locations[0].get("relative_path", "") if locations else "",
                "soname": elf.get("soname", ""),
                "architecture": elf.get("machine", ""),
                "bits": elf.get("bits", 0),
                "needed": needed,
            }
        )
    return {
        "schema_version": 1,
        "generated_at": utc_now(),
        "workspace": str(workspace.root),
        "elf_count": len(nodes),
        "nodes": nodes,
        "unresolved": sorted(unresolved),
    }


def render_capabilities(report: dict[str, Any]) -> str:
    lines = ["# Firmware capability map", ""]
    for group, records in report["groups"].items():
        lines.extend([f"## {group}", ""])
        if not records:
            lines.extend(["No matching artifacts inventoried.", ""])
            continue
        lines.extend(["| Artifact | Architecture | Evidence |", "| --- | --- | --- |"])
        for record in records:
            architecture = record["architecture"]
            if record["bits"]:
                architecture = f"{architecture}/{record['bits']}"
            evidence = ", ".join(record["matched"])
            lines.append(
                f"| `{record['path']}` (`{record['sha256'][:12]}`) | {architecture} | {evidence} |"
            )
        lines.append("")
    return "\n".join(lines)


def render_dependencies(report: dict[str, Any]) -> str:
    lines = [
        "# ELF dependency map",
        "",
        f"Inventoried ELF files: {report['elf_count']}",
        "",
        "## Unresolved dependencies",
        "",
    ]
    if report["unresolved"]:
        lines.extend(f"- `{name}`" for name in report["unresolved"])
    else:
        lines.append("All recorded dependencies have an inventoried provider.")
    lines.extend(["", "## Artifacts", ""])
    for node in report["nodes"]:
        dependencies = ", ".join(item["name"] for item in node["needed"]) or "none"
        lines.append(f"- `{node['path']}`: {dependencies}")
    lines.append("")
    return "\n".join(lines)


def write_report(report: dict[str, Any], output: Path, markdown: str | None = None) -> Path:
    output.parent.mkdir(parents=True, exist_ok=True)
    if markdown is None:
        output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        output.write_text(markdown, encoding="utf-8")
    return output
