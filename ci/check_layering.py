#!/usr/bin/env python3
"""Layering guards: core/ must never reference Godot (AGENTS.md, plan §2.2),
the indexed atlas must keep its authoritative storage one-byte-per-texel
(M4 slice 4 RGBA tripwire), structural derivation must stay asset-free
(M6 slice 1 separation pin), and the production FauxBuildView must stay a
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

# M6 slice 1 separation pin: the structural world derives from MAP data
# ALONE. Geometry and raw appearance must never depend on the asset side
# (atlas, asset set, ART, palette): tile dimensions and indexed texels meet
# the world at the later rendering/presentation seam (M6.2), not inside
# core structural derivation. build_structural_world must stay callable
# without any assets loaded.
structural_files = [
    core / "include/fauxbuild/structural.hpp",
    core / "src/structural.cpp",
]
asset_include = re.compile(r'#\s*include\s*[<"]fauxbuild/(atlas|asset_set|art|palette)\.hpp')
for path in structural_files:
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if asset_include.search(line):
            violations.append(f"{path.relative_to(root)}:{lineno}: structural derivation must "
                              f"not depend on the asset side: {line.strip()}")

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

# M6.1 shared slope authority: `heinum` may appear ONLY inside the marked
# evaluator region of structural.cpp. A second slope equation anywhere in the
# derivation -- even one that happens to agree today -- is the defect this
# pins, because the two drift apart the first time either is touched.
structural_cpp = root / "core/src/structural.cpp"
structural_text = structural_cpp.read_text(encoding="utf-8")
begin_marker = "// --- slope evaluator (single authority) ---"
end_marker = "// --- end slope evaluator ---"
begin = structural_text.find(begin_marker)
end = structural_text.find(end_marker)
if begin < 0 or end < 0 or end < begin:
    violations.append("core/src/structural.cpp: the slope evaluator's region markers are "
                      "missing; the single-authority tripwire cannot run")
else:
    for lineno, line in enumerate(structural_text.splitlines(), 1):
        if "heinum" not in line:
            continue
        offset = structural_text.find(line)
        if offset < begin or offset > end:
            violations.append(
                f"core/src/structural.cpp:{lineno}: slope arithmetic must live only in the "
                f"one evaluator (M6.1): {line.strip()}")
    if "surface_z_at(" not in structural_text[end:]:
        violations.append("core/src/structural.cpp: geometry generation must call "
                          "surface_z_at; no sloped vertex may be placed another way")

if violations:
    print("layering check FAILED:")
    for v in violations:
        print(f"  {v}")
    sys.exit(1)

print("layering check: core/ contains no Godot references; atlas payload is indexed; "
      "slope arithmetic has one authority; "
      "structural derivation stays asset-free; FauxBuildView consumes StructuralWorld "
      "only; the content route lives in FauxStructuralSource")
