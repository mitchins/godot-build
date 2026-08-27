#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fauxbuild/atlas.hpp"
#include "fauxbuild/palette.hpp"
#include "fauxbuild/result.hpp"
#include "fauxbuild/structural.hpp"

namespace fauxbuild {

// D0020 prepared render world (M6 slice 2A). The seam where derived geometry
// meets derived assets:
//
//   StructuralWorld + IndexedAtlas -> PreparedWorld -> FauxBuildView
//
// Pure C++: no Godot type appears here or in prepared.cpp, and structural.*
// stays asset-free — the dependency runs one way, from this layer onto both
// inputs, never back.
//
// Preparation is NOT allowed to change geometry. Vertices and indices are
// copied through verbatim, in the same order, and a CI gate pins that. All
// this layer adds is one UV per vertex plus the atlas page each surface
// samples.

// UVs are TILE-LOCAL: 1.0 is one whole tile repeat, and values outside [0,1]
// are normal and mean the tile tiles. The consumer wraps within the tile's
// atlas rect (below) rather than across the whole page, which is what makes
// repetition work inside an atlas at all.
struct PreparedUV {
    float u = 0.0f;
    float v = 0.0f;

    bool operator==(const PreparedUV&) const = default;
};

struct PreparedSurface {
    SurfaceKind kind = SurfaceKind::Floor;
    std::int16_t sector = 0;
    std::int16_t wall = -1;
    std::int32_t page = 0;   // atlas page this surface samples
    std::int32_t picnum = 0; // resolved source tile
    // The tile's rect within its page, page-normalized. The consumer applies
    // it verbatim; it is atlas bookkeeping, not UV interpretation.
    float rect_x = 0.0f;
    float rect_y = 0.0f;
    float rect_w = 0.0f;
    float rect_h = 0.0f;

    // Verbatim from the StructuralSurface this was prepared from.
    std::vector<StructuralVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Exactly one per vertex, in the same order.
    std::vector<PreparedUV> uvs;

    SurfaceAppearance appearance;

    bool operator==(const PreparedSurface&) const = default;
};

struct PreparedWorld {
    std::vector<PreparedSurface> surfaces;
    // The authoritative indexed pages, R8: one palette index per byte. No
    // RGBA form is authoritative anywhere; a consumer may derive one for
    // preview but must not feed it back.
    std::vector<std::uint8_t> atlas_pixels;
    std::int32_t page_width = 0;
    std::int32_t page_height = 0;
    std::int32_t page_count = 0;
    // 256 RGB triples of the base palette, expanded to 8-bit.
    std::vector<std::uint8_t> palette_rgb;

    bool operator==(const PreparedWorld&) const = default;
};

// ---------------------------------------------------------------------------
// THE UV authority (D0020). Every provisional convention lives in this one
// struct, so changing a constant, a sign, or an axis assignment later is a
// single-site edit — which the M6.2A black-box program was going to settle and
// which the human visual gate now settles instead.
//
// **These are PROVISIONAL. They are not proven, and nothing may present them
// as established.** The approved documentation establishes that repeat is a
// pixel-size control and panning is an alignment offset; it does NOT establish
// the world-to-texel scale, the U/V direction conventions, or the default
// repeat. Each field below records what it assumes.
struct UvConventions {
    // PROVISIONAL: world XY units spanned by one texel on floors and ceilings
    // at default flags. No approved source states a texel size in world units.
    double floor_units_per_texel = 16.0;

    // PROVISIONAL: world XY units spanned by one texel along a wall's U axis
    // at the reference repeat below.
    double wall_units_per_texel_u = 16.0;

    // PROVISIONAL: Build Z units spanned by one texel along a wall's V axis at
    // the reference repeat. The ratified 16:1 metric says 16 Z units are one
    // XY unit, so this being 16x the U constant is what makes texels square.
    double wall_z_per_texel_v = 256.0;

    // PROVISIONAL: the repeat value the two constants above are stated at.
    // The numeric default repeat is NOT established (missing fact 5); this is
    // a reference point for scaling, not a claim about defaults.
    double reference_repeat = 64.0;

    // PROVISIONAL: U increases along the wall's own direction A->B.
    bool wall_u_follows_wall_direction = true;
    // PROVISIONAL: V increases downward in Build Z (walls anchored at top).
    bool wall_v_increases_with_build_z = true;
    // PROVISIONAL: floor U/V take world X/Y respectively, unswapped.
    bool floor_u_is_world_x = true;

    // --- Authored placement controls (M6.2B1). The BITS are documented
    // (PROVENANCE row 9); how they combine below is the provisional generic
    // model, applied only in core/src/prepared.cpp:
    //
    //   1. base (a,b) coordinates: world Build X/Y, or — with the
    //      relative-alignment bit — the sector's first-wall frame;
    //   2. the swap-XY bit exchanges a and b;
    //   3. tile-local u = a / units_u, v = b / units_v (the M6.2A scale);
    //   4. a flip bit NEGATES its tile-local coordinate (a mirror; the
    //      consumer's fract() makes -u the exact mirror of u);
    //   5. panning adds tile-local phase AFTER flips, so a flip cannot
    //      erase or double a pan.
    //
    // PROVISIONAL: panning bytes are texel offsets within the selected tile
    // (pan_u = xpanning / tile_width, pan_v = ypanning / tile_height;
    // wrapping stays tile-local). The SIGN is one global choice for all
    // surfaces: true adds phase (texture appears to move toward -U/-V).
    bool panning_adds_phase = true;

    // PROVISIONAL: with the relative-alignment bit, floor/ceiling U runs
    // along the sector's first wall A->B (false: B->A)...
    bool floor_relative_u_follows_first_wall = true;
    // ...and V runs along the LEFT perpendicular of the (possibly reversed) U
    // direction — reversing U reverses V with it, so the frame stays
    // right-handed. Origin is the first wall's A endpoint.
    bool floor_relative_v_is_left_perp = true;

    // Must match the StructuralOptions::scale the world was built with.
    // Not a UV convention — it is how render-space vertices are read back to
    // Build coordinates, which is where every constant above is stated.
    double render_scale = 1.0 / 2048.0;

    bool operator==(const UvConventions&) const = default;
};

// Compose geometry and assets. Fails with a structured error when a surface's
// picnum is out of range or unpopulated — deliberately, rather than silently
// substituting a placeholder tile.
Result<PreparedWorld> prepare_world(const StructuralWorld& world, const IndexedAtlas& atlas,
                                    const PaletteData& palette,
                                    const UvConventions& conventions = {});

} // namespace fauxbuild
