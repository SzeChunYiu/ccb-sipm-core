#!/usr/bin/env python3
"""Fail closed on unresolved Git conflict markers in executable/config text.

The scanner operates on tracked files only. Markdown/plain-text documentation is
excluded deliberately so historical prose can quote marker syntax without
blocking builds; compiled sources, scripts, CI/config files, and CMake inputs
remain in scope.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

SCANNED_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx",
    ".h", ".hh", ".hpp", ".hxx",
    ".cmake", ".py", ".sh",
    ".yml", ".yaml", ".json", ".toml", ".ini", ".cfg",
}
SCANNED_NAMES = {"CMakeLists.txt", ".gitmodules"}
MARKERS = (b"<" * 7, b"=" * 7, b">" * 7)


def is_scanned_path(path: Path) -> bool:
    return path.name in SCANNED_NAMES or path.suffix.lower() in SCANNED_SUFFIXES


def conflict_lines(data: bytes) -> list[tuple[int, bytes]]:
    """Return 1-based lines whose first non-whitespace token is a Git marker."""
    hits: list[tuple[int, bytes]] = []
    for lineno, raw in enumerate(data.splitlines(), start=1):
        stripped = raw.lstrip(b" \t")
        if any(stripped.startswith(marker) for marker in MARKERS):
            hits.append((lineno, raw))
    return hits


def tracked_files(root: Path) -> list[Path]:
    proc = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        check=True,
        stdout=subprocess.PIPE,
    )
    return [root / Path(p.decode("utf-8")) for p in proc.stdout.split(b"\0") if p]


def scan_repository(root: Path) -> list[tuple[Path, int, bytes]]:
    findings: list[tuple[Path, int, bytes]] = []
    for path in tracked_files(root):
        if not is_scanned_path(path) or not path.is_file():
            continue
        data = path.read_bytes()
        if b"\0" in data:
            continue
        for lineno, raw in conflict_lines(data):
            findings.append((path.relative_to(root), lineno, raw))
    return findings


def run_self_test() -> None:
    clean = b'int main() { return 0; }\nconst char* s = "' + (b"<" * 7) + b'";\n'
    assert conflict_lines(clean) == []
    for marker in MARKERS:
        sample = b"ok\n  " + marker + b" branch\nend\n"
        hits = conflict_lines(sample)
        assert len(hits) == 1 and hits[0][0] == 2
    # Six-character near misses are not Git conflict delimiters.
    assert conflict_lines((b"<" * 6) + b" HEAD\n") == []


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        print("conflict-marker guard self-test: PASS")
        return 0

    root = Path(__file__).resolve().parents[1]
    findings = scan_repository(root)
    if findings:
        print("unresolved Git conflict markers detected:", file=sys.stderr)
        for path, lineno, raw in findings:
            preview = raw.decode("utf-8", errors="replace")
            print(f"  {path}:{lineno}: {preview}", file=sys.stderr)
        return 1

    print("conflict-marker guard: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
