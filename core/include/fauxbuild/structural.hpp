#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fauxbuild/map_v7.hpp"
#include "fauxbuild/result.hpp"

namespace fauxbuild {

// M5 slice 1: presentation-oriented static structural geometry derived from
// authoritative MapData. This is a disposable derived cache (RENDERING_CONTRACT,
// D0016): the world can be rebuilt at any time from the map alone, and no part
// of it may become world authority. Pure C++ — no Godot types here; the M5
// slice-2 viewer consumes render-space vertices verbatim.

enum class SurfaceKind {
    Floor,
    Ceiling,
    SolidWall,
    PortalUpper,
    PortalLower,
};

const char* surface_kind_name(SurfaceKind kind);

struct StructuralVertex {
    // Render space (D0016): right-handed, Y up. Exact by construction — see
    // to_render_space.
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool operator==(const StructuralVertex&) const = default;
};

// M6 slice 1 appearance contract: the raw MAP appearance facts for one
// emitted surface, preserved verbatim for the later texture/UV seam. Nothing
// here is interpreted by slice 1 — no UVs, no flag behaviour, no Duke
// semantics; a field whose meaning is not yet established stays raw. Fields
// per surface kind:
//
//   Floor/Ceiling: picnum, shade, pal, raw_stat (the sector's
//                  floorstat/ceilingstat), xpanning, ypanning.
//   Wall spans:    picnum, overpicnum, shade, pal, raw_stat (the wall's
//                  cstat), xrepeat, yrepeat, xpanning, ypanning.
//
// Fields that do NOT belong here: heinum (a geometry input for the slope
// evaluator, not appearance), lotag/hitag/extra and anything else
// game-specific (tags are not appearance).
//
// Intended M6.2 seam (defined, deliberately NOT implemented in slice 1):
//
//   StructuralWorld (geometry + this raw appearance)
//       + IndexedAtlas / AssetSet (tile dimensions, indexed texels)
//       -> UV/material packing
//       -> Godot ArrayMesh + indexed shader
//
// Geometry and raw appearance derive from MapData alone; tile dimensions and
// texels live on the asset side; the two meet at the rendering/presentation
// seam, never inside this header. Adding appearance here must never require
// atlas/asset includes in the structural core.
struct SurfaceAppearance {
    std::int16_t picnum = 0;
    std::int16_t overpicnum = 0; // wall spans only; 0 on floors/ceilings
    // The raw stat word: floorstat/ceilingstat for Floor/Ceiling, wall cstat
    // for wall spans. Preserved verbatim; interpretation (beyond the slope
    // bit's geometry role once the evaluator exists) belongs to later slices.
    std::int16_t raw_stat = 0;
    std::int8_t shade = 0;
    std::uint8_t pal = 0;
    std::uint8_t xpanning = 0;
    std::uint8_t ypanning = 0;
    std::uint8_t xrepeat = 0; // wall spans only; 0 on floors/ceilings
    std::uint8_t yrepeat = 0; // wall spans only

    bool operator==(const SurfaceAppearance&) const = default;
};

struct StructuralSurface {
    SurfaceKind kind = SurfaceKind::Floor;
    std::int16_t sector = 0; // owning sector
    std::int16_t wall = -1;  // owning wall; -1 for floor/ceiling
    // Raw appearance facts for the M6 texture/UV seam (see SurfaceAppearance).
    // Inert for geometry: nothing here affects slice-1 vertex placement.
    SurfaceAppearance appearance;

    std::vector<StructuralVertex> vertices; // render space
    std::vector<std::uint32_t> indices;     // triangles, three indices each

    bool operator==(const StructuralSurface&) const = default;
};

// Non-fatal, actionable diagnostics: features M5 deliberately defers (slopes,
// wall cstat semantics) and odd-but-representable content (inverted vertical
// intervals). Distinct from Result errors, which are fatal geometry failures.
struct StructuralNote {
    std::string record; // e.g. "sector[3]", "wall[17]"
    std::string detail;

    bool operator==(const StructuralNote&) const = default;
};

// A derived surface that could not be produced from otherwise-valid topology
// (D0018). Distinct from StructuralNote (deferred features) and from Result
// errors (unsafe or inconsistent topology): the map is fine, this particular
// derived surface is degenerate. The world is still built; the surface is
// omitted. Real shipped content contains zero-area sectors, and one of them
// must not cost the other 99.9% of the shell.
struct StructuralDiagnostic {
    std::string record;  // e.g. "sector[262]"
    std::string surface; // e.g. "floor", "ceiling"
    std::string reason;  // stable machine-comparable token, e.g. "zero_area"

    bool operator==(const StructuralDiagnostic&) const = default;
};

// Raw sector-level appearance, one entry per SOURCE sector in source order.
// M6 preserves the value; **M10 owns its behavioural and render
// interpretation** (RENDERING_CONTRACT: palette/lookup/shade/visibility
// shader). It lives here because the M6.2 seam consumes a StructuralWorld
// and nothing else — a sector-scoped shading input with no route through
// this type would be unreachable when M10 needs it, and the seam is far
// harder to widen once several milestones depend on it.
//
// Sector-scoped, so it is NOT duplicated into SurfaceAppearance: a surface
// finds its value through its own `sector` index. Copied verbatim; nothing
// interprets it, no traversal or visibility determination exists, and no
// asset/atlas dependency is implied.
struct StructuralSectorAppearance {
    std::uint8_t visibility = 0;

    bool operator==(const StructuralSectorAppearance&) const = default;
};

struct StructuralWorld {
    // Canonical order (tested): sector ascending; per sector floor, ceiling,
    // then walls ascending by wall index; a portal wall contributes
    // portal_upper before portal_lower. Nothing else may reorder surfaces.
    std::vector<StructuralSurface> surfaces;
    // Exactly one entry per source sector, in source order: index it with a
    // surface's `sector`. Present for every sector, including ones that emit
    // no surface, so the index correspondence is total.
    std::vector<StructuralSectorAppearance> sector_appearance;
    std::vector<StructuralNote> notes;
    std::vector<StructuralDiagnostic> diagnostics;

    bool operator==(const StructuralWorld&) const = default;
};

// Build vertical (Z) coordinates are numerically 16x the horizontal (X/Y)
// scale for the same physical extent: 1024 horizontal units and 16384
// vertical units describe the same distance. Treating the three axes
// isotropically stretches every world into a tower (D0016 amendment,
// accepted 2026-08-25 — see docs/DECISIONS.md).
//
// This is a compatibility invariant of the format, NOT user tuning, so it is
// a compile-time constant rather than an option: there is deliberately no
// independent z_scale for a caller to get wrong. It is a power of two, so it
// preserves the exactness/reversibility property below.
inline constexpr double kBuildVerticalUnitsPerHorizontal = 16.0;

struct StructuralOptions {
    // Uniform HORIZONTAL render scale (D0016). Must be a power of two so
    // every Build int32 coordinate maps to an exactly representable double
    // and back — vertex bytes are then bit-identical across platforms and
    // rebuilds. 2^-11: one 65536-unit grid square -> 32 render units. The
    // vertical scale is derived from it, never set independently.
    double scale = 1.0 / 2048.0;
};

// THE Build-space -> render-space conversion (D0016; one place, tested; no
// consumer may invent its own transform). Build Z points down, render Y up,
// and Build Z is 16x the horizontal unit scale:
//   render.x =  build.x * scale
//   render.y = -build.z * scale / 16
//   render.z =  build.y * scale
// So 1024 horizontal units and 16384 vertical units produce equal render
// lengths. Both factors are powers of two, so the mapping stays exact for
// all int32 inputs and reversible by multiplying with the matching inverse
// and rounding toward zero.
StructuralVertex to_render_space(std::int32_t x, std::int32_t y, std::int32_t z,
                                 const StructuralOptions& options = {});

// Derive the static structural shell of a validated MAP v7 world: floors and
// ceilings triangulated with holes preserved, solid walls as single vertical
// spans, portal walls as upper/lower spans outside the vertical opening only
// (never a full quad across the opening). Sloped sectors are emitted flat at
// their base Z with a deferral note (M6 owns slope semantics). Malformed or
// geometrically unrepresentable content yields structured errors; the input
// MapData is never mutated. Requires validate_map-clean input; the first
// validation error is surfaced as the structured error otherwise.
Result<StructuralWorld> build_structural_world(const mapv7::MapData& map,
                                               const StructuralOptions& options = {});

} // namespace fauxbuild
