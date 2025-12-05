#!/usr/bin/env python3
"""Fail fast if any binary assets are tracked by Git.

This guard is portable (no `file` dependency) and should be used before
sending pull requests to ensure the diff remains text-only. It detects
binary-looking extensions and NUL bytes in the first 8 KiB of each tracked
file. Extend `BINARY_EXTENSIONS` if new asset types need to be blocked.
"""
from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Iterable

# Extensions we do not want in version control.
BINARY_EXTENSIONS = {
    ".bin",
    ".elf",
    ".exe",
    ".a",
    ".o",
    ".dll",
    ".so",
    ".dylib",
    ".hex",
    ".out",
    ".jpg",
    ".jpeg",
    ".png",
    ".bmp",
    ".ico",
    ".gif",
    ".ttf",
    ".otf",
    ".woff",
    ".woff2",
    ".mp3",
    ".mp4",
    ".wav",
    ".zip",
    ".gz",
    ".tar",
}


def git_tracked_files() -> Iterable[Path]:
    """Yield all files tracked by Git (ignoring submodules)."""
    output = subprocess.check_output(["git", "ls-files"], text=True)
    for line in output.splitlines():
        path = Path(line)
        if path.is_file():
            yield path


def looks_binary(path: Path) -> bool:
    """Heuristic binary detection based on extension and NUL bytes."""
    if path.suffix.lower() in BINARY_EXTENSIONS:
        return True

    try:
        with path.open("rb") as fh:
            chunk = fh.read(8192)
            return b"\0" in chunk
    except OSError:
        # If the file cannot be read, err on the side of caution.
        return True


def main() -> int:
    violations = [str(p) for p in git_tracked_files() if looks_binary(p)]
    if violations:
        print("❌ Binary files detected in Git (please remove/revert):")
        for path in violations:
            print(f" - {path}")
        return 1

    print("✅ No binary assets tracked by Git.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
