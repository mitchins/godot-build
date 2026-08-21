#!/usr/bin/env python3
"""Layering guard: core/ must never include Godot headers (AGENTS.md, plan §2.2)."""

import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
core = root / "core"
forbidden = re.compile(r'#\s*include\s*[<"](godot|godot_cpp|gdextension)', re.IGNORECASE)

violations = []
for path in sorted(core.rglob("*")):
    if path.suffix not in (".h", ".hpp", ".cpp"):
        continue
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if forbidden.search(line):
            violations.append(f"{path.relative_to(root)}:{lineno}: {line.strip()}")

if violations:
    print("layering check FAILED: Godot headers referenced in core/")
    for v in violations:
        print(f"  {v}")
    sys.exit(1)

print("layering check: core/ contains no Godot headers")
