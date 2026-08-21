#!/usr/bin/env python3
"""Format guard: verify tracked C++ sources against .clang-format.

Skips with a warning when clang-format is not installed, so the gate keeps
working on minimal machines; installs are expected on developer machines.
"""

import pathlib
import shutil
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent

if shutil.which("clang-format") is None:
    print("format-check: clang-format not installed; skipping "
          "(install with: brew install clang-format)")
    sys.exit(0)

files = []
for directory in ("core", "tools", "tests", "extension"):
    base = root / directory
    if base.exists():
        files.extend(p for p in base.rglob("*") if p.suffix in (".h", ".hpp", ".cpp"))

failed = []
for path in sorted(files):
    result = subprocess.run(
        ["clang-format", "--dry-run", "--Werror", str(path)],
        capture_output=True, text=True, cwd=root,
    )
    if result.returncode != 0:
        failed.append(path.relative_to(root))
        print(result.stderr.strip())

if failed:
    print("format-check FAILED:")
    for f in failed:
        print(f"  {f}")
    sys.exit(1)

print(f"format-check: {len(files)} files conform to .clang-format")
