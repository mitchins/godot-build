#!/usr/bin/env python3
"""Layering guards: core/ must never reference Godot (AGENTS.md, plan §2.2),
and the indexed atlas must keep its authoritative storage one-byte-per-texel
(M4 slice 4 RGBA tripwire)."""

import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
core = root / "core"
include = re.compile(r'#\s*include\s*[<"](godot|godot_cpp|gdextension)', re.IGNORECASE)
qualified = re.compile(r'\bgodot(_cpp)?::')

violations = []
for path in sorted(core.rglob("*")):
    if path.suffix not in (".h", ".hpp", ".cpp"):
        continue
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if include.search(line) or qualified.search(line):
            violations.append(f"{path.relative_to(root)}:{lineno}: {line.strip()}")

# Contract pin: the authoritative atlas payload stays std::uint8_t indices
# (atlas.hpp). If this declaration moves or changes type, the guard fails
# until the change is consciously propagated (docs + tripwire tests).
atlas_hpp = core / "include/fauxbuild/atlas.hpp"
pin = re.compile(r'std::vector<std::uint8_t>\s+pixels\s*;')
if not pin.search(atlas_hpp.read_text(encoding="utf-8")):
    violations.append("core/include/fauxbuild/atlas.hpp: authoritative "
                      "'std::vector<std::uint8_t> pixels;' declaration missing")

if violations:
    print("layering check FAILED:")
    for v in violations:
        print(f"  {v}")
    sys.exit(1)

print("layering check: core/ contains no Godot references; atlas payload is indexed")
