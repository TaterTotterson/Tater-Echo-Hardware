"""Strictly read-only ADB evidence collection."""

from __future__ import annotations

import json
import re
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .workspace import Workspace, WorkspaceError, utc_now


TEXT_COMMANDS = (
    ("getprop", "getprop"),
    ("uname", "uname -a"),
    ("cpuinfo", "cat /proc/cpuinfo"),
    ("meminfo", "cat /proc/meminfo"),
    ("cmdline", "cat /proc/cmdline"),
    ("mounts", "cat /proc/mounts"),
    ("partitions", "cat /proc/partitions"),
    ("modules", "cat /proc/modules"),
    ("devices", "cat /proc/devices"),
    ("interrupts", "cat /proc/interrupts"),
    ("asound-cards", "cat /proc/asound/cards"),
    ("asound-pcm", "cat /proc/asound/pcm"),
    ("asound-devices", "cat /proc/asound/devices"),
    ("device-nodes", "find /dev -maxdepth 2 -mindepth 1 -print 2>/dev/null | sort"),
    (
        "media-nodes",
        "ls -la /dev/dri /dev/ion /dev/video* /dev/media* /dev/mtk* /dev/vcodec* /dev/mali* 2>/dev/null",
    ),
    (
        "sound-sysfs",
        "find /sys/class/sound -maxdepth 3 -type f -o -type l 2>/dev/null | sort",
    ),
    (
        "graphics-sysfs",
        "find /sys/class/graphics /sys/class/drm /sys/class/video4linux -maxdepth 3 -print 2>/dev/null | sort",
    ),
    ("device-tree-model", "cat /proc/device-tree/model 2>/dev/null"),
    ("device-tree-compatible", "cat /proc/device-tree/compatible 2>/dev/null"),
    ("android-codecs", "dumpsys media.codec 2>/dev/null"),
    ("android-audio", "dumpsys media.audio_flinger 2>/dev/null"),
    ("android-display", "dumpsys SurfaceFlinger 2>/dev/null"),
    ("android-hals", "lshal 2>/dev/null"),
)


def _adb(serial: str | None) -> list[str]:
    command = ["adb"]
    if serial:
        command.extend(["-s", serial])
    return command


def attached_devices() -> list[str]:
    if not shutil.which("adb"):
        raise WorkspaceError("adb was not found")
    result = subprocess.run(
        ["adb", "devices"], capture_output=True, text=True, check=False, timeout=15
    )
    if result.returncode:
        raise WorkspaceError(f"adb devices failed: {result.stderr.strip()}")
    devices = []
    for line in result.stdout.splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "device":
            devices.append(fields[0])
    return devices


def choose_device(serial: str | None) -> str:
    devices = attached_devices()
    if serial:
        if serial not in devices:
            raise WorkspaceError(f"ADB device {serial!r} is not attached and authorized")
        return serial
    if not devices:
        raise WorkspaceError("no authorized ADB device is attached")
    if len(devices) > 1:
        raise WorkspaceError("multiple ADB devices are attached; pass --serial")
    return devices[0]


def snapshot(workspace: Workspace, serial: str | None = None) -> Path:
    workspace.initialize()
    selected = choose_device(serial)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    safe_serial = re.sub(r"[^A-Za-z0-9_.-]+", "_", selected)[:100] or "unknown"
    output = workspace.root / "traces" / f"device-{safe_serial}-{stamp}"
    output.mkdir(parents=True, exist_ok=False)

    records: list[dict[str, Any]] = []
    for label, shell_command in TEXT_COMMANDS:
        result = subprocess.run(
            [*_adb(selected), "shell", shell_command],
            capture_output=True,
            check=False,
            timeout=45,
        )
        destination = output / f"{label}.txt"
        combined = result.stdout
        if result.stderr:
            combined += b"\n--- stderr ---\n" + result.stderr
        destination.write_bytes(combined)
        records.append(
            {
                "label": label,
                "command": shell_command,
                "exit_code": result.returncode,
                "file": destination.name,
                "bytes": len(combined),
            }
        )

    config_result = subprocess.run(
        [*_adb(selected), "exec-out", "sh", "-c", "test -r /proc/config.gz && cat /proc/config.gz"],
        capture_output=True,
        check=False,
        timeout=30,
    )
    if config_result.stdout:
        (output / "proc-config.gz").write_bytes(config_result.stdout)
        records.append(
            {
                "label": "proc-config",
                "command": "read /proc/config.gz",
                "exit_code": config_result.returncode,
                "file": "proc-config.gz",
                "bytes": len(config_result.stdout),
            }
        )

    metadata = {
        "schema_version": 1,
        "collected_at": utc_now(),
        "serial": selected,
        "mode": "read-only",
        "records": records,
    }
    (output / "snapshot.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return output

