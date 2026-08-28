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

// The masked-wall binary cutout sentinel (M6.2C1b), HUMAN-RATIFIED
// 2026-08-28 for the currently supported palette/compatibility profile.
//
// The approved documentation (PROVENANCE row 16, InfoSuite) establishes the
// SEMANTIC — a texture may contain a pink colour whose pixels disappear on
// masked walls and stay opaque on ordinary surfaces — but establishes NO
// numeric index. The index was resolved by black-box measurement over legally
// owned content through this project's own loaders, never from source
// archaeology, and the measurement is the evidence:
//
//   - TWO base-palette entries are exact full magenta, 245 and 255, so RGB
//     alone cannot identify the sentinel. This is an INDEX semantic, not
//     "the colour magenta": index 245 is NOT transparent despite matching.
//   - index 245: 0 texels in the masked-overpicnum tile population, and
//     absent from every surface tile the six owned maps exercise.
//   - index 255: 32060 texels = 44.66% of that population, in 8 of its 10
//     tiles.
//   - floor and ceiling tiles (211 tiles, 1.42M texels) contain ZERO index
//     255 — authors never place it where it cannot cut out.
//   - tiles containing index 255 are used BOTH as masked overpicnums and as
//     ordinary wall picnums, which proves cutout is per-SURFACE behaviour and
//     never a property of the tile.
//   - some masked tiles contain no index 255 at all, so masked never implies
//     whole-surface transparency.
//
// SCOPE: ratified for the current supported compatibility profile only. It is
// NOT claimed as a universal invariant of every Build-derived palette; if
// later legally supported content disproves it, reopen the convention. This
// is the single site to change (docs/DECISIONS.md D0021).
inline constexpr std::uint8_t kMaskedCutoutIndex = 255;

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

    // M6.2C1b: the ONE render fact the consumer needs for cutout behaviour.
    // True only for prepared masked portal layers. The consumer selects its
    // presentation from THIS flag — it must not rediscover the masked cstat
    // bit, overpicnum, or the surface kind to decide (pinned by
    // ci/check_layering.py and by the boundary gate).
    //
    // It is a property of the SURFACE, not of the tile: the same tile is used
    // both as a masked overlay and as an ordinary wall texture in owned
    // content, and must cut out in the first case and stay opaque in the
    // second.
    bool cutout_enabled = false;

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
    // M6.2C1b: the single centralized palette-level cutout fact, carried once
    // for the whole world rather than per surface. A cutout-enabled surface
    // discards exactly the texels whose INDEX equals this value; the decision
    // is made on the authoritative R8 index and never on palette RGB, because
    // two palette entries share the sentinel's colour (see kMaskedCutoutIndex).
    std::uint8_t transparent_index = kMaskedCutoutIndex;

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
//
// M6.2C1: this seam owns THE effective-texture selection, centralized and
// explicit — ordinary wall kinds resolve appearance.picnum, PortalMasked
// resolves appearance.overpicnum (the documented masked/one-way overlay tile).
// overpicnum == 0 is tile 0, never "no overlay": no approved provenance
// establishes a sentinel. A nonzero overpicnum on an ordinary surface selects
// nothing. The consumer receives the resolved picnum/page/rect and must not
// re-derive any of it (pinned by ci/check_layering.py).
//
// `world` is caller-provided, so its per-sector tables are validated as
// EXTERNAL input before any surface is prepared, never assumed and never
// FB_CHECKed: `sector_frames` must cover the same sector index domain as
// `sector_appearance`, and every surface's `sector` must index that domain.
// A world that fails either check yields a structured error and NO partial
// PreparedWorld — preparing surfaces from absent or default frame data would
// silently produce wrong UVs instead of a diagnosable failure.
Result<PreparedWorld> prepare_world(const StructuralWorld& world, const IndexedAtlas& atlas,
                                    const PaletteData& palette,
                                    const UvConventions& conventions = {});

} // namespace fauxbuild
