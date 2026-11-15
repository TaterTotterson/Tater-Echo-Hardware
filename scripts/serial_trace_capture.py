"""Original bounded serial exchange with durable partial diagnostics.

The caller supplies an already selected serial transport and a reviewed,
target-guarded command. This module neither chooses a device nor retries a
command. Logs must remain owner-local: they may include private device data.
"""
from dataclasses import dataclass
import os
from pathlib import Path
import secrets
import time


@dataclass(frozen=True)
class TraceResult:
    completed: bool
    timed_out: bool
    limit_reached: bool
    output: str
    captured_bytes: int


def capture_exchange(serial, command, log_path, *, timeout=10, max_bytes=131072):
    """Never discard partial output when a command fails to return.

    A completed frame is only transport completion, not a hardware pass.
    Deliberately no CLI taking arbitrary commands or implicit device choice.
    """
    if not 0 < timeout <= 60 or not 256 <= max_bytes <= 1048576:
        raise ValueError("bounded timeout and log size required")
    if not isinstance(command, str) or not command or any(c in command for c in "\0\r\n"):
        raise ValueError("one reviewed shell command required")
    if len(command.encode("utf-8")) > 3072:
        raise ValueError("command exceeds serial line bound")
    path = Path(log_path)
    if not path.is_absolute():
        raise ValueError("explicit absolute owner-local log path required")
    # Exclusive creation and no symlink following: never replace another log.
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
    captured = bytearray()
    tag = secrets.token_hex(8)
    begin, end = "__ISP_BEGIN_" + tag + "__", "__ISP_END_" + tag + "__"
    complete = False
    limit = False
    deadline = time.monotonic() + timeout
    last_sync = 0.0
    try:
        with os.fdopen(fd, "wb", buffering=0) as log:
            serial.write(("b=%s; e=%s; echo $b; %s; echo $e\n" % (begin, end, command)).encode())
            while time.monotonic() < deadline:
                chunk = serial.read()
                if not chunk:
                    time.sleep(0.01)
                    continue
                remaining = max_bytes - len(captured)
                chunk = chunk[:remaining]
                captured.extend(chunk)
                view = memoryview(chunk)
                while view:
                    written = log.write(view)
                    if written is None or written <= 0:
                        raise OSError("short serial log write")
                    view = view[written:]
                now = time.monotonic()
                if now - last_sync >= 0.05:
                    os.fsync(log.fileno())
                    last_sync = now
                lines = captured.decode("utf-8", "replace").replace("\r", "").split("\n")
                if begin in lines and end in lines[lines.index(begin) + 1:]:
                    complete = True
                    break
                if len(captured) >= max_bytes:
                    limit = True
                    break
            os.fsync(log.fileno())
    finally:
        # Ownership of the serial connection remains with the caller, which
        # can inspect/recover the shell without blindly replaying the command.
        pass
    lines = captured.decode("utf-8", "replace").replace("\r", "").split("\n")
    if begin in lines:
        lines = lines[lines.index(begin) + 1:]
        if complete:
            lines = lines[:lines.index(end)]
    return TraceResult(complete, not complete and not limit, limit, "\n".join(lines), len(captured))
