"""Small dependency-free binary text search."""

from __future__ import annotations

import mmap
import re
from pathlib import Path
from typing import Any


def search_strings(
    path: Path, query: str, minimum: int = 4, limit: int = 100
) -> list[dict[str, Any]]:
    """Search printable ASCII strings while retaining exact file offsets."""

    minimum = max(minimum, 1)
    expression = re.compile(rb"[\x20-\x7e]{" + str(minimum).encode("ascii") + rb",}")
    needle = query.casefold()
    matches = []
    with path.open("rb") as source:
        if source.seek(0, 2) == 0:
            return matches
        source.seek(0)
        with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
            for match in expression.finditer(data):
                value = match.group().decode("ascii")
                if needle not in value.casefold():
                    continue
                matches.append(
                    {
                        "offset": match.start(),
                        "offset_hex": f"0x{match.start():x}",
                        "value": value,
                    }
                )
                if len(matches) >= max(limit, 1):
                    break
    return matches
