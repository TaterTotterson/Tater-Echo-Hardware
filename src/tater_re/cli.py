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
from . import binary, device, ghidra, images, reports
from .artifacts import classify, sha256_file
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

    strings = commands.add_parser("strings", help="search printable strings in one artifact")
    strings.add_argument("artifact")
    strings.add_argument("query")
    strings.add_argument("--minimum", type=int, default=4)
    strings.add_argument("--limit", type=int, default=100)

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
    ghidra_export = ghidra_commands.add_parser(
        "export", help="refresh JSONL from an existing analyzed project"
    )
    ghidra_export.add_argument("artifact")
    ghidra_export.add_argument("--ghidra-home")
    ghidra_search = ghidra_commands.add_parser(
        "search", help="search exported functions, calls, signatures, and strings"
    )
    ghidra_search.add_argument("query")
    ghidra_search.add_argument("--artifact", help="optional SHA-256 prefix")
    ghidra_search.add_argument("--limit", type=int, default=100)
    ghidra_batch = ghidra_commands.add_parser(
        "batch", help="analyze matching ELF artifacts that have not been exported"
    )
    ghidra_batch.add_argument("query")
    ghidra_batch.add_argument("--limit", type=int, default=20)
    ghidra_batch.add_argument("--ghidra-home")

    image_parser = commands.add_parser("image", help="inspect and extract offline firmware images")
    image_commands = image_parser.add_subparsers(dest="image_command", required=True)
    image_inspect = image_commands.add_parser("inspect", help="inspect an image header")
    image_inspect.add_argument("path", type=Path)
    image_list = image_commands.add_parser("list", help="list an ext4 image")
    image_list.add_argument("path", type=Path)
    image_list.add_argument("--root", default="/")
    image_list.add_argument("--recursive", action="store_true")
    image_list.add_argument("--json", action="store_true")
    image_extract = image_commands.add_parser(
        "extract", help="extract matching regular files from an ext4 image"
    )
    image_extract.add_argument("path", type=Path)
    image_extract.add_argument("patterns", nargs="+")
    image_extract.add_argument("--root", default="/")
    image_extract.add_argument("--output", type=Path)
    boot_extract = image_commands.add_parser(
        "boot-extract", help="split an Android boot image and unpack its ramdisk"
    )
    boot_extract.add_argument("path", type=Path)
    boot_extract.add_argument("--output", type=Path)

    report_parser = commands.add_parser("report", help="build offline evidence reports")
    report_commands = report_parser.add_subparsers(dest="report_command", required=True)
    for name in ("capabilities", "dependencies"):
        report_command = report_commands.add_parser(name)
        report_command.add_argument("--format", choices=("json", "markdown"), default="markdown")
        report_command.add_argument("--output", type=Path)

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
    elif args.command == "strings":
        digest, artifact = workspace.resolve(args.artifact)
        path = workspace.existing_path(artifact)
        print(
            json.dumps(
                {
                    "sha256": digest,
                    "path": str(path),
                    "matches": binary.search_strings(path, args.query, args.minimum, args.limit),
                },
                indent=2,
                sort_keys=True,
            )
        )
    elif args.command == "ghidra":
        if args.ghidra_command == "status":
            print(json.dumps(ghidra.status(args.ghidra_home), indent=2, sort_keys=True))
        elif args.ghidra_command == "analyze":
            print(ghidra.analyze(workspace, args.artifact, args.ghidra_home))
        elif args.ghidra_command == "decompile":
            print(ghidra.decompile(workspace, args.artifact, args.selector, args.ghidra_home))
        elif args.ghidra_command == "export":
            print(ghidra.export_project(workspace, args.artifact, args.ghidra_home))
        elif args.ghidra_command == "search":
            print(
                json.dumps(
                    ghidra.search_exports(workspace, args.query, args.artifact, args.limit),
                    indent=2,
                    sort_keys=True,
                )
            )
        elif args.ghidra_command == "batch":
            results = {"analyzed": [], "skipped": [], "errors": []}
            matches = [
                (digest, artifact)
                for digest, artifact in workspace.search(args.query)
                if artifact.get("kind") == "elf"
            ][: max(args.limit, 0)]
            for digest, artifact in matches:
                export = (
                    workspace.root / "analysis" / "ghidra" / digest / "program.jsonl"
                )
                if artifact.get("analysis", {}).get("ghidra") and export.is_file():
                    results["skipped"].append(digest)
                    continue
                try:
                    ghidra.analyze(workspace, digest, args.ghidra_home)
                    results["analyzed"].append(digest)
                except WorkspaceError as error:
                    results["errors"].append({"sha256": digest, "error": str(error)})
            print(json.dumps(results, indent=2, sort_keys=True))
    elif args.command == "image":
        path = args.path.expanduser().resolve()
        if args.image_command == "inspect":
            result = classify(path)
            with path.open("rb") as source:
                source.seek(512)
                is_gpt = source.read(8) == images.GPT_MAGIC
            if result["kind"] == "android-boot-image":
                result["android_boot"] = images.android_boot_metadata(path)
            if is_gpt:
                result["kind"] = "gpt"
                result["gpt"] = images.gpt_metadata(path)
            result.update({"path": str(path), "size": path.stat().st_size})
            print(json.dumps(result, indent=2, sort_keys=True))
        elif args.image_command == "list":
            entries = (
                images.ext4_walk(path, args.root)
                if args.recursive
                else images.ext4_list(path, args.root)
            )
            if args.json:
                print(json.dumps(entries, indent=2, sort_keys=True))
            else:
                for entry in entries:
                    print(
                        f"{entry['kind']:<10} {human_size(entry['size']):>10}  {entry['path']}"
                    )
        elif args.image_command == "extract":
            if args.output:
                output = args.output.expanduser().resolve()
            else:
                digest = sha256_file(path)[:16]
                output = workspace.root / "extracted" / f"{path.stem}-{digest}"
            extracted = images.extract_ext4(path, args.patterns, output, args.root)
            print(json.dumps({"output": str(output), "files": extracted}, indent=2))
        elif args.image_command == "boot-extract":
            if args.output:
                output = args.output.expanduser().resolve()
            else:
                digest = sha256_file(path)[:16]
                output = workspace.root / "extracted" / f"{path.stem}-{digest}"
            print(json.dumps(images.extract_android_boot(path, output), indent=2, sort_keys=True))
    elif args.command == "report":
        if args.report_command == "capabilities":
            report = reports.capability_report(workspace)
            markdown = reports.render_capabilities(report)
        else:
            report = reports.dependency_report(workspace)
            markdown = reports.render_dependencies(report)
        content = json.dumps(report, indent=2, sort_keys=True) + "\n" if args.format == "json" else markdown
        if args.output:
            output = args.output.expanduser().resolve()
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(content, encoding="utf-8")
            print(output)
        else:
            print(content, end="" if content.endswith("\n") else "\n")
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

