"""Command-line interface for the Tater research workbench."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

from . import __version__
from . import device, ghidra
from .workspace import Workspace, WorkspaceError, human_size


def _workspace(args: argparse.Namespace) -> Workspace:
    return Workspace(Path(args.workspace))


def _record(digest: str, artifact: dict[str, Any]) -> dict[str, Any]:
    elf = artifact.get("metadata", {}).get("elf", {})
    locations = artifact.get("locations", [])
    return {
        "sha256": digest,
        "size": artifact.get("size", 0),
        "kind": artifact.get("kind", ""),
        "architecture": elf.get("machine", ""),
        "bits": elf.get("bits", ""),
        "soname": elf.get("soname", ""),
        "needed": elf.get("needed", []),
        "path": locations[0].get("relative_path", "") if locations else "",
        "locations": locations,
    }


def _print_records(records: list[tuple[str, dict[str, Any]]], as_json: bool) -> None:
    values = [_record(digest, artifact) for digest, artifact in records]
    if as_json:
        print(json.dumps(values, indent=2, sort_keys=True))
        return
    if not values:
        print("No artifacts found.")
        return
    for value in values:
        architecture = value["architecture"]
        if value["bits"]:
            architecture = f"{architecture}/{value['bits']}"
        print(
            f"{value['sha256'][:12]}  {value['kind']:<24} "
            f"{architecture:<14} {human_size(value['size']):>10}  {value['path']}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="tater-re", description="Local Tater firmware and hardware research workbench"
    )
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    parser.add_argument(
        "--workspace",
        default=os.environ.get("TATER_RE_WORKSPACE", ".tater-re"),
        help="ignored local research workspace (default: .tater-re)",
    )
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("init", help="initialize a local research workspace")

    ingest = commands.add_parser("ingest", help="inventory a file or extracted firmware directory")
    ingest.add_argument("input", type=Path)

    inventory = commands.add_parser("inventory", help="list known artifacts")
    inventory.add_argument("--json", action="store_true")

    search = commands.add_parser("search", help="search paths and ELF metadata")
    search.add_argument("query")
    search.add_argument("--json", action="store_true")

    show = commands.add_parser("show", help="show one artifact by SHA-256 prefix")
    show.add_argument("artifact")

    ghidra_parser = commands.add_parser("ghidra", help="run local Ghidra headless analysis")
    ghidra_commands = ghidra_parser.add_subparsers(dest="ghidra_command", required=True)
    ghidra_status = ghidra_commands.add_parser("status", help="locate Ghidra")
    ghidra_status.add_argument("--ghidra-home")
    ghidra_analyze = ghidra_commands.add_parser("analyze", help="analyze and export an ELF")
    ghidra_analyze.add_argument("artifact")
    ghidra_analyze.add_argument("--ghidra-home")
    ghidra_decompile = ghidra_commands.add_parser(
        "decompile", help="decompile one function from an analyzed ELF"
    )
    ghidra_decompile.add_argument("artifact")
    ghidra_decompile.add_argument("selector", help="function name or address")
    ghidra_decompile.add_argument("--ghidra-home")
    ghidra_search = ghidra_commands.add_parser(
        "search", help="search exported functions, calls, signatures, and strings"
    )
    ghidra_search.add_argument("query")
    ghidra_search.add_argument("--artifact", help="optional SHA-256 prefix")
    ghidra_search.add_argument("--limit", type=int, default=100)

    device_parser = commands.add_parser("device", help="collect read-only hardware evidence")
    device_commands = device_parser.add_subparsers(dest="device_command", required=True)
    device_snapshot = device_commands.add_parser("snapshot", help="take a read-only ADB snapshot")
    device_snapshot.add_argument("--serial")
    return parser


def run(args: argparse.Namespace) -> int:
    workspace = _workspace(args)
    if args.command == "init":
        workspace.initialize()
        print(f"Initialized research workspace: {workspace.root}")
    elif args.command == "ingest":
        result = workspace.ingest(args.input)
        print(
            f"Scanned {result['scanned']} files; added {result['added']} unique artifacts "
            f"and {result['occurrences']} locations."
        )
    elif args.command == "inventory":
        _print_records(workspace.records(), args.json)
    elif args.command == "search":
        _print_records(workspace.search(args.query), args.json)
    elif args.command == "show":
        digest, artifact = workspace.resolve(args.artifact)
        print(json.dumps(_record(digest, artifact) | {"raw": artifact}, indent=2, sort_keys=True))
    elif args.command == "ghidra":
        if args.ghidra_command == "status":
            print(json.dumps(ghidra.status(args.ghidra_home), indent=2, sort_keys=True))
        elif args.ghidra_command == "analyze":
            print(ghidra.analyze(workspace, args.artifact, args.ghidra_home))
        elif args.ghidra_command == "decompile":
            print(ghidra.decompile(workspace, args.artifact, args.selector, args.ghidra_home))
        elif args.ghidra_command == "search":
            print(
                json.dumps(
                    ghidra.search_exports(workspace, args.query, args.artifact, args.limit),
                    indent=2,
                    sort_keys=True,
                )
            )
    elif args.command == "device" and args.device_command == "snapshot":
        print(device.snapshot(workspace, args.serial))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    try:
        return run(parser.parse_args(argv))
    except (WorkspaceError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"tater-re: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

