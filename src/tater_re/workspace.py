"""Local manifest and firmware inventory."""

from __future__ import annotations

import hashlib
import json
import os
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

from .artifacts import classify, sha256_file


SCHEMA_VERSION = 1
SKIP_DIRECTORIES = {".git", ".tater-re", "__pycache__"}


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def human_size(value: int) -> str:
    size = float(value)
    for suffix in ("B", "KiB", "MiB", "GiB", "TiB"):
        if size < 1024 or suffix == "TiB":
            return f"{size:.0f} {suffix}" if suffix == "B" else f"{size:.1f} {suffix}"
        size /= 1024
    return f"{value} B"


class WorkspaceError(RuntimeError):
    pass


class Workspace:
    def __init__(self, root: Path):
        self.root = root.expanduser().resolve()
        self.manifest_path = self.root / "manifest.json"

    def initialize(self) -> None:
        self.root.mkdir(parents=True, exist_ok=True)
        for child in ("analysis", "ghidra", "notes", "traces"):
            (self.root / child).mkdir(exist_ok=True)
        if not self.manifest_path.exists():
            now = utc_now()
            self.save(
                {
                    "schema_version": SCHEMA_VERSION,
                    "created_at": now,
                    "updated_at": now,
                    "sources": {},
                    "artifacts": {},
                }
            )

    def load(self) -> dict[str, Any]:
        if not self.manifest_path.exists():
            raise WorkspaceError(f"workspace is not initialized: {self.root}")
        manifest = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        if manifest.get("schema_version") != SCHEMA_VERSION:
            raise WorkspaceError("unsupported workspace manifest version")
        return manifest

    def save(self, manifest: dict[str, Any]) -> None:
        self.root.mkdir(parents=True, exist_ok=True)
        manifest["updated_at"] = utc_now()
        handle, temporary = tempfile.mkstemp(prefix="manifest-", suffix=".json", dir=self.root)
        try:
            with os.fdopen(handle, "w", encoding="utf-8") as output:
                json.dump(manifest, output, indent=2, sort_keys=True)
                output.write("\n")
            os.replace(temporary, self.manifest_path)
        except Exception:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass
            raise

    def _files(self, source: Path) -> Iterable[tuple[Path, str]]:
        if source.is_file():
            yield source, source.name
            return
        if not source.is_dir():
            raise WorkspaceError(f"input does not exist or is not a regular file/directory: {source}")
        for directory, names, files in os.walk(source, followlinks=False):
            names[:] = sorted(name for name in names if name not in SKIP_DIRECTORIES)
            base = Path(directory)
            for name in sorted(files):
                path = base / name
                if path.is_symlink() or not path.is_file():
                    continue
                yield path, path.relative_to(source).as_posix()

    def ingest(self, input_path: Path) -> dict[str, int]:
        source = input_path.expanduser().resolve()
        self.initialize()
        manifest = self.load()
        source_id = hashlib.sha256(str(source).encode()).hexdigest()[:16]
        manifest["sources"][source_id] = {
            "id": source_id,
            "path": str(source),
            "kind": "directory" if source.is_dir() else "file",
            "ingested_at": utc_now(),
        }

        scanned = 0
        added = 0
        occurrences = 0
        for path, relative in self._files(source):
            scanned += 1
            digest = sha256_file(path)
            artifact = manifest["artifacts"].get(digest)
            if artifact is None:
                metadata = classify(path)
                artifact = {
                    "sha256": digest,
                    "size": path.stat().st_size,
                    "kind": metadata.pop("kind"),
                    "metadata": metadata,
                    "locations": [],
                    "first_seen_at": utc_now(),
                }
                manifest["artifacts"][digest] = artifact
                added += 1
            location = {
                "source_id": source_id,
                "relative_path": relative,
                "path": str(path),
            }
            if location not in artifact["locations"]:
                artifact["locations"].append(location)
                occurrences += 1

        self.save(manifest)
        return {"scanned": scanned, "added": added, "occurrences": occurrences}

    def resolve(self, prefix: str) -> tuple[str, dict[str, Any]]:
        manifest = self.load()
        normalized = prefix.lower().removeprefix("sha256:")
        matches = [(digest, value) for digest, value in manifest["artifacts"].items() if digest.startswith(normalized)]
        if not matches:
            raise WorkspaceError(f"no artifact matches {prefix!r}")
        if len(matches) > 1:
            raise WorkspaceError(f"artifact prefix {prefix!r} is ambiguous ({len(matches)} matches)")
        return matches[0]

    def existing_path(self, artifact: dict[str, Any]) -> Path:
        for location in artifact.get("locations", []):
            candidate = Path(location["path"])
            if candidate.is_file():
                return candidate
        raise WorkspaceError("none of this artifact's recorded input paths still exists")

    def records(self) -> list[tuple[str, dict[str, Any]]]:
        return sorted(self.load()["artifacts"].items(), key=lambda item: self.display_path(item[1]))

    @staticmethod
    def display_path(artifact: dict[str, Any]) -> str:
        locations = artifact.get("locations", [])
        return locations[0]["relative_path"] if locations else ""

    def search(self, query: str) -> list[tuple[str, dict[str, Any]]]:
        needle = query.casefold()
        results = []
        for digest, artifact in self.records():
            elf = artifact.get("metadata", {}).get("elf", {})
            fields = [
                digest,
                artifact.get("kind", ""),
                elf.get("machine", ""),
                elf.get("soname", ""),
                elf.get("interpreter", ""),
                *elf.get("needed", []),
                *(location.get("relative_path", "") for location in artifact.get("locations", [])),
            ]
            if needle in "\n".join(str(field) for field in fields).casefold():
                results.append((digest, artifact))
        return results

