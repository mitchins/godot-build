#!/usr/bin/env python3
"""Format guard: verify tracked C++ sources against .clang-format.

On developer machines without clang-format the check skips with a warning.
With FAUXBUILD_STRICT_TOOLS=1 (CI runners) a missing clang-format is a hard
failure, so the gate cannot pass silently on a bare machine.
"""

import os
import pathlib
import shutil
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
strict = os.environ.get("FAUXBUILD_STRICT_TOOLS") == "1"

if shutil.which("clang-format") is None:
    if strict:
        print("format-check FAILED: clang-format not installed and "
              "FAUXBUILD_STRICT_TOOLS=1 is set")
        sys.exit(1)
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
