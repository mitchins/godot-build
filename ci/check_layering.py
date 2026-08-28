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
# fauxbuild::StructuralWorld and (M6.2A, D0020) fauxbuild::PreparedWorld: it
# must not include any other core header,
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
    r'to_render_space|GrpMount|Vfs|AssetSet|IndexedAtlas|ResourceSaver|'
    r'prepare_world|UvConventions|units_per_texel|repeat_factor)\b')
for path in view_files:
    text = path.read_text(encoding="utf-8")
    for inc in view_include.findall(text):
        if inc not in ("fauxbuild/structural.hpp", "fauxbuild/prepared.hpp"):
            violations.append(f"{path.relative_to(root)}: core include '{inc}' not allowed "
                              "in FauxBuildView (structural.hpp / prepared.hpp only)")
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
    # Track each line's ACTUAL offset. Searching for the line's text would
    # resolve a repeated line to its first occurrence, so an out-of-region
    # `heinum` line identical to one inside the evaluator would silently
    # inherit the inside offset and escape this gate entirely.
    offset = 0
    for lineno, line in enumerate(structural_text.splitlines(keepends=True), 1):
        line_offset = offset
        offset += len(line)
        line = line.rstrip("\n")
        if "heinum" not in line:
            continue
        if line_offset < begin or line_offset > end:
            violations.append(
                f"core/src/structural.cpp:{lineno}: slope arithmetic must live only in the "
                f"one evaluator (M6.1): {line.strip()}")
    if "surface_z_at(" not in structural_text[end:]:
        violations.append("core/src/structural.cpp: geometry generation must call "
                          "surface_z_at; no sloped vertex may be placed another way")

# M6.2A UV single authority (D0020): every provisional UV convention lives in
# core/src/prepared.cpp and nowhere else. A second UV computation -- especially
# one in the view, which would silently diverge from the prepared arrays it is
# supposed to upload verbatim -- is the defect this pins.
uv_authority = root / "core/src/prepared.cpp"
if not uv_authority.exists():
    violations.append("core/src/prepared.cpp is missing; the UV authority cannot be located")
else:
    uv_tokens = re.compile(r'\b(units_per_texel|units_per_tile|wall_z_per_texel_v|'
                           r'reference_repeat|repeat_factor)\b')
    for path in sorted(root.glob("core/src/*.cpp")) + sorted(root.glob("extension/src/*.cpp")):
        if path == uv_authority:
            continue
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if uv_tokens.search(line):
                violations.append(
                    f"{path.relative_to(root)}:{lineno}: UV interpretation must live only in "
                    f"core/src/prepared.cpp (D0020): {line.strip()}")

# M6.2B1 authored-frame pin: relative floor/ceiling alignment must consume the
# sector's first-wall frame as COPIED by the structural derivation
# (StructuralWorld::sector_frames), never reconstruct a first wall from
# emitted wall surfaces or wall-list topology inside the UV authority. The
# reconstruction route needs the wall-list terms below; prepared.cpp sees
# neither MapData nor the wall list, only the verbatim frame.
if uv_authority.exists():
    frame_tokens = re.compile(r'\b(wallptr|wallnum|point2|nextwall|first_wall_search)\b')
    for lineno, line in enumerate(uv_authority.read_text(encoding="utf-8").splitlines(), 1):
        if frame_tokens.search(line):
            violations.append(
                f"core/src/prepared.cpp:{lineno}: the UV authority must consume "
                f"StructuralWorld::sector_frames, not reconstruct the first wall: "
                f"{line.strip()}")
    # And the frame table itself is produced only by the structural core.
    frame_owner = re.compile(r'sector_frames\s*(=|\.push_back|\.resize|\.assign|\.reserve)')
    for path in sorted(core.rglob("*.cpp")) + sorted(core.rglob("*.hpp")):
        if path == core / "src/structural.cpp" or path == core / "include/fauxbuild/structural.hpp":
            continue
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if frame_owner.search(line):
                violations.append(
                    f"{path.relative_to(root)}:{lineno}: sector_frames is structural "
                    f"derivation output; only structural.cpp may fill it: {line.strip()}")

# M6.2B1: authored placement bits are interpreted ONLY in the UV authority.
# The view must not read placement fields at all; the structural core may
# reference a bit solely to emit diagnostics (the degenerate relative-frame
# note), never to place anything. The named constants exist so this pin is
# greppable. Exempt: FauxStructuralFixture AUTHORS synthetic test content —
# it writes a MAP with placement bits set, exactly like map fixtures set
# slope bits; authoring content is not interpreting it.
#
# LIMIT OF THIS PIN — do not mistake it for the guarantee. It greps for the
# NAMED constants, so it is a readability aid and a static tripwire against
# the obvious mistake, nothing more. It cannot see a raw numeric placement
# bit: `stat & 0x0008` in the view passes this check. The actual authority
# guarantee is BEHAVIOURAL — godot/scripts/textured_boundary_test.gd proves
# FauxBuildView uploads the PreparedWorld UVs verbatim, on
# placement-carrying content, so any reinterpretation in the view fails
# there however it is spelled. Strengthen that gate, not this grep; this is
# deliberately not a parser.
placement_tokens = re.compile(
    r'\b(kStatPlaneSwapXY|kStatPlaneSmoosh|kStatPlaneFlipX|kStatPlaneFlipY|'
    r'kStatPlaneRelative|kWallCstatBottomAligned|kWallCstatFlipX|kWallCstatFlipY)\b')
fixture_harness = root / "extension/src/faux_structural_fixture.cpp"
for path in sorted(root.glob("core/src/*.cpp")) + sorted(root.glob("extension/src/*.cpp")):
    if path == uv_authority or path == core / "src/structural.cpp" or path == fixture_harness:
        continue
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if placement_tokens.search(line):
            violations.append(
                f"{path.relative_to(root)}:{lineno}: placement-bit interpretation belongs "
                f"only to the UV authority (M6.2B1): {line.strip()}")

if violations:
    print("layering check FAILED:")
    for v in violations:
        print(f"  {v}")
    sys.exit(1)

print("layering check: core/ contains no Godot references; atlas payload is indexed; "
      "slope arithmetic has one authority; UV interpretation has one authority; "
      "authored frames/placement bits live in their owners; "
      "structural derivation stays asset-free; FauxBuildView consumes StructuralWorld "
      "only; the content route lives in FauxStructuralSource")
