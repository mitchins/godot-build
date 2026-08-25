#!/usr/bin/env python3
"""Layering guards: core/ must never reference Godot (AGENTS.md, plan §2.2),
the indexed atlas must keep its authoritative storage one-byte-per-texel
(M4 slice 4 RGBA tripwire), and the production FauxBuildView must stay a
consumer of fauxbuild::StructuralWorld only (M5 slice 2 viewer guard)."""

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

# M5 slice 2 viewer guard: pin the architectural seam, not a style. The
# production FauxBuildView is a pure consumer of
# fauxbuild::StructuralWorld: it must not include any other core header,
# and must not reference world-production facilities (map parsing, fixture
# synthesis, structural derivation, the core render conversion, VFS/GRP or
# asset loading) or generated-scene persistence. The test/sample harness
# (faux_structural_fixture.cpp) legitimately produces worlds from committed
# fixtures and is deliberately outside this guard's file list.
view_files = [
    root / "extension/include/fauxbuild_godot/fauxbuild_view.hpp",
    root / "extension/src/fauxbuild_view.cpp",
]
view_include = re.compile(r'#\s*include\s*[<"](fauxbuild/[A-Za-z0-9_./]+\.hpp)')
viewer_forbidden = re.compile(
    r'\b(map_v7|mapv7|map_synth|map_io|map_validate|build_structural_world|'
    r'to_render_space|GrpMount|Vfs|AssetSet|IndexedAtlas|ResourceSaver)\b')
for path in view_files:
    text = path.read_text(encoding="utf-8")
    for inc in view_include.findall(text):
        if inc != "fauxbuild/structural.hpp":
            violations.append(f"{path.relative_to(root)}: core include '{inc}' not allowed "
                              "in FauxBuildView (structural.hpp only)")
    for lineno, line in enumerate(text.splitlines(), 1):
        if viewer_forbidden.search(line):
            violations.append(f"{path.relative_to(root)}:{lineno}: FauxBuildView must not "
                              f"reference world production: {line.strip()}")

# M5 slice 3 source-owner guard: pin that the real-content route
# (mount -> VFS -> MAP reader -> structural derivation -> view seam) lives
# in FauxStructuralSource, and only there. The .cpp must include the
# route's core headers and must hand worlds to the view's presentation
# seam; neither file may synthesize fixtures, validate separately,
# touch assets/textures, or build Godot meshes itself (that would bypass
# FauxBuildView — the source renders nothing). Fixture synthesis (map_synth,
# grp_synth) is test infrastructure: the production owner must mount and parse
# real bytes, never manufacture them.
source_files = [
    root / "extension/include/fauxbuild_godot/faux_structural_source.hpp",
    root / "extension/src/faux_structural_source.cpp",
]
source_forbidden = re.compile(
    r'\b(map_synth|grp_synth|map_validate|AssetSet|IndexedAtlas|ResourceSaver|read_art|read_palette|'
    r'read_lookup|ArrayMesh|MeshInstance3D|memnew|add_surface_from_arrays|write_map)\b')
for path in source_files:
    text = path.read_text(encoding="utf-8")
    for lineno, line in enumerate(text.splitlines(), 1):
        if source_forbidden.search(line):
            violations.append(f"{path.relative_to(root)}:{lineno}: FauxStructuralSource must "
                              f"not do this (production route owner only): {line.strip()}")
source_cpp = (root / "extension/src/faux_structural_source.cpp").read_text(encoding="utf-8")
for required in ("fauxbuild/map_io.hpp", "fauxbuild/vfs.hpp", "fauxbuild/structural.hpp"):
    if f'#include "{required}"' not in source_cpp:
        violations.append(f"extension/src/faux_structural_source.cpp: missing required core "
                          f"include '{required}' (the production route must live here)")
if "present_world(" not in source_cpp:
    violations.append("extension/src/faux_structural_source.cpp: must hand worlds to "
                      "FauxBuildView.present_world (the view seam), not present on its own")

if violations:
    print("layering check FAILED:")
    for v in violations:
        print(f"  {v}")
    sys.exit(1)

print("layering check: core/ contains no Godot references; atlas payload is indexed; "
      "FauxBuildView consumes StructuralWorld only; the content route lives in "
      "FauxStructuralSource")
