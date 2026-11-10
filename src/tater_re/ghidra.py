"""Ghidra headless integration."""

from __future__ import annotations

import glob
import json
import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any

from .workspace import Workspace, WorkspaceError, utc_now


def _headless_candidate(value: Path) -> Path | None:
    candidates = [
        value,
        value / "support" / "analyzeHeadless",
        value / "Contents" / "Resources" / "support" / "analyzeHeadless",
        value / "libexec" / "support" / "analyzeHeadless",
    ]
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate.resolve()
    return None


def find_analyze_headless(explicit: str | None = None) -> Path | None:
    if explicit:
        return _headless_candidate(Path(explicit).expanduser())
    configured = os.environ.get("GHIDRA_HOME")
    if configured:
        found = _headless_candidate(Path(configured).expanduser())
        if found:
            return found
    command = shutil.which("analyzeHeadless")
    if command:
        return Path(command).resolve()
    patterns = (
        "/Applications/ghidra*/support/analyzeHeadless",
        "/Applications/Ghidra*.app/Contents/Resources/support/analyzeHeadless",
        "/opt/homebrew/Cellar/ghidra/*/libexec/support/analyzeHeadless",
        "/usr/local/Cellar/ghidra/*/libexec/support/analyzeHeadless",
    )
    for pattern in patterns:
        for value in sorted(glob.glob(pattern), reverse=True):
            found = _headless_candidate(Path(value))
            if found:
                return found
    return None


def script_directory() -> Path:
    return Path(__file__).resolve().parents[2] / "scripts" / "ghidra"


def project_directory(workspace: Workspace) -> Path:
    """Keep projects out of a dot-directory, which Ghidra rejects by design."""

    return workspace.root.parent / "tater-re-ghidra-projects"


def find_java_home() -> Path | None:
    configured = os.environ.get("JAVA_HOME")
    if configured and (Path(configured) / "bin" / "java").is_file():
        return Path(configured).resolve()
    candidates = (
        "/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home",
        "/usr/local/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home",
        "/opt/homebrew/opt/openjdk/libexec/openjdk.jdk/Contents/Home",
        "/usr/local/opt/openjdk/libexec/openjdk.jdk/Contents/Home",
    )
    for value in candidates:
        candidate = Path(value)
        if (candidate / "bin" / "java").is_file():
            return candidate.resolve()
    return None


def _run(command: list[str], log_path: Path) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    java_home = find_java_home()
    if java_home:
        environment["JAVA_HOME"] = str(java_home)
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run(
            command,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
            env=environment,
        )
    if result.returncode:
        raise WorkspaceError(f"Ghidra exited with status {result.returncode}; see {log_path}")


def _validate_export(export_path: Path, log_path: Path) -> None:
    """Reject a successful Ghidra process when its post-script did not run."""

    try:
        with export_path.open("r", encoding="utf-8") as source:
            first_line = source.readline()
        record = json.loads(first_line)
    except (FileNotFoundError, OSError, json.JSONDecodeError) as error:
        raise WorkspaceError(
            f"Ghidra did not produce a valid analysis export; see {log_path}"
        ) from error
    if record.get("record") != "program":
        raise WorkspaceError(f"Ghidra analysis export is incomplete; see {log_path}")


def analyze(
    workspace: Workspace, artifact_prefix: str, ghidra_home: str | None = None
) -> Path:
    executable = find_analyze_headless(ghidra_home)
    if not executable:
        raise WorkspaceError("Ghidra analyzeHeadless was not found; pass --ghidra-home")
    digest, artifact = workspace.resolve(artifact_prefix)
    source = workspace.existing_path(artifact)
    if artifact.get("kind") != "elf":
        raise WorkspaceError("the first Ghidra adapter currently accepts ELF artifacts only")

    project_root = project_directory(workspace)
    project_root.mkdir(parents=True, exist_ok=True)
    project_name = f"artifact_{digest[:16]}"
    output_root = workspace.root / "analysis" / "ghidra" / digest
    output_root.mkdir(parents=True, exist_ok=True)
    export_path = output_root / "program.jsonl"
    log_path = output_root / "analyze.log"

    command = [
        str(executable),
        str(project_root),
        project_name,
        "-import",
        str(source),
        "-overwrite",
        "-analysisTimeoutPerFile",
        "900",
        "-scriptPath",
        str(script_directory()),
        "-postScript",
        "ExportProgram.java",
        str(export_path),
    ]
    _run(command, log_path)
    _validate_export(export_path, log_path)

    manifest = workspace.load()
    manifest["artifacts"][digest].setdefault("analysis", {})["ghidra"] = {
        "analyzed_at": utc_now(),
        "project_name": project_name,
        "program_name": source.name,
        "export_path": str(export_path),
        "log_path": str(log_path),
    }
    workspace.save(manifest)
    return export_path


def decompile(
    workspace: Workspace,
    artifact_prefix: str,
    selector: str,
    ghidra_home: str | None = None,
) -> Path:
    executable = find_analyze_headless(ghidra_home)
    if not executable:
        raise WorkspaceError("Ghidra analyzeHeadless was not found; pass --ghidra-home")
    digest, artifact = workspace.resolve(artifact_prefix)
    ghidra = artifact.get("analysis", {}).get("ghidra")
    if not ghidra:
        raise WorkspaceError("artifact has not been analyzed by Ghidra yet")

    safe_selector = re.sub(r"[^A-Za-z0-9_.-]+", "_", selector)[:100] or "function"
    output_root = workspace.root / "analysis" / "ghidra" / digest
    output_path = output_root / f"decompile-{safe_selector}.json"
    log_path = output_root / f"decompile-{safe_selector}.log"
    project_root = project_directory(workspace)
    command = [
        str(executable),
        str(project_root),
        ghidra["project_name"],
        "-process",
        ghidra["program_name"],
        "-noanalysis",
        "-scriptPath",
        str(script_directory()),
        "-postScript",
        "DecompileFunction.java",
        selector,
        str(output_path),
    ]
    _run(command, log_path)
    try:
        json.loads(output_path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError) as error:
        raise WorkspaceError(f"Ghidra did not produce a valid decompilation: {error}") from error
    return output_path


def export_project(
    workspace: Workspace, artifact_prefix: str, ghidra_home: str | None = None
) -> Path:
    """Refresh the JSONL export from an existing analyzed Ghidra project."""

    executable = find_analyze_headless(ghidra_home)
    if not executable:
        raise WorkspaceError("Ghidra analyzeHeadless was not found; pass --ghidra-home")
    digest, artifact = workspace.resolve(artifact_prefix)
    ghidra = artifact.get("analysis", {}).get("ghidra")
    if not ghidra:
        raise WorkspaceError("artifact has not been analyzed by Ghidra yet")
    output_root = workspace.root / "analysis" / "ghidra" / digest
    export_path = output_root / "program.jsonl"
    log_path = output_root / "export.log"
    command = [
        str(executable),
        str(project_directory(workspace)),
        ghidra["project_name"],
        "-process",
        ghidra["program_name"],
        "-noanalysis",
        "-scriptPath",
        str(script_directory()),
        "-postScript",
        "ExportProgram.java",
        str(export_path),
    ]
    _run(command, log_path)
    _validate_export(export_path, log_path)
    return export_path


def status(explicit: str | None = None) -> dict[str, Any]:
    executable = find_analyze_headless(explicit)
    java_home = find_java_home()
    return {
        "available": executable is not None,
        "analyze_headless": str(executable) if executable else "",
        "java_home": str(java_home) if java_home else "",
        "script_directory": str(script_directory()),
    }


def search_exports(
    workspace: Workspace,
    query: str,
    artifact_prefix: str | None = None,
    limit: int = 100,
) -> list[dict[str, Any]]:
    if artifact_prefix:
        digest, _artifact = workspace.resolve(artifact_prefix)
        exports = [workspace.root / "analysis" / "ghidra" / digest / "program.jsonl"]
    else:
        exports = sorted((workspace.root / "analysis" / "ghidra").glob("*/program.jsonl"))
    needle = query.casefold()
    matches: list[dict[str, Any]] = []
    for export in exports:
        if not export.is_file():
            continue
        digest = export.parent.name
        with export.open("r", encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                if needle not in line.casefold():
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    record = {"record": "invalid", "raw": line.rstrip("\n")}
                record["artifact_sha256"] = digest
                record["line"] = line_number
                matches.append(record)
                if len(matches) >= max(limit, 1):
                    return matches
    return matches

