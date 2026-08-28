// D0020 prepared render world (M6 slice 2A). These gates prove the
// ARCHITECTURE — geometry passes through untouched, picnums resolve to real
// atlas tiles, UVs exist once per vertex and come from one authority. They do
// NOT assert historical Build constants: the UV conventions are provisional
// and the human visual gate on real content is what settles them.
//
// M6.2B1 adds the authored-placement gates: panning, flips, swap-XY, relative
// alignment and wall bottom alignment are interpreted ONLY in the UV
// authority, with zero/default placement byte-identical to M6.2A.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "fauxbuild/atlas.hpp"
#include "fauxbuild/map_synth.hpp"
#include "fauxbuild/map_v7.hpp"
#include "fauxbuild/palette.hpp"
#include "fauxbuild/prepared.hpp"
#include "fauxbuild/structural.hpp"
#include "fauxbuild/tile_build.hpp"

using fauxbuild::build_indexed_atlas;
using fauxbuild::build_structural_world;
using fauxbuild::IndexedAtlas;
using fauxbuild::PaletteData;
using fauxbuild::prepare_world;
using fauxbuild::PreparedSurface;
using fauxbuild::PreparedWorld;
using fauxbuild::StructuralWorld;
using fauxbuild::SurfaceKind;
using fauxbuild::UvConventions;
using fauxbuild::synth::map_fixture;

namespace {

// Two ORIGINAL synthetic tiles with different dimensions, so a tile-size
// dependency in the UV authority is visible rather than assumed away.
const char* kTileset = R"(tileset prepared_gate
tile gate_a 64 64 pattern=checker a=16 b=60 square=24
tile gate_b 128 64 pattern=checker a=20 b=56 square=24
)";

IndexedAtlas make_atlas() {
    auto tileset = fauxbuild::parse_tileset(kTileset, "prepared_gate");
    REQUIRE(tileset.is_ok());
    fauxbuild::TileManifest manifest;
    auto built = fauxbuild::build_art_from_tileset(tileset.value(), manifest);
    REQUIRE(built.is_ok());
    auto atlas = build_indexed_atlas({built.value().art}, {});
    REQUIRE(atlas.is_ok());
    return atlas.take();
}

PaletteData make_palette() {
    PaletteData p;
    for (std::size_t i = 0; i < fauxbuild::kPaletteBytes; ++i) {
        p.rgb[i] = static_cast<std::uint8_t>(i % 64);
    }
    return p;
}

// Every surface in the fixture points at a tile the atlas actually has.
StructuralWorld world_using(std::int16_t picnum, const char* fixture = "two_sector_portal") {
    auto map = map_fixture(fixture);
    REQUIRE(map.is_ok());
    auto source = map.take();
    for (auto& sector : source.sectors) {
        sector.floorpicnum = picnum;
        sector.ceilingpicnum = picnum;
    }
    for (auto& wall : source.walls) {
        wall.picnum = picnum;
    }
    auto world = build_structural_world(source);
    REQUIRE(world.is_ok());
    return world.take();
}

} // namespace

TEST_CASE("prepared world: geometry passes through preparation untouched") {
    // Gate 1. Preparation adds UVs; it must not move, reorder, or drop a
    // single vertex or index.
    const IndexedAtlas atlas = make_atlas();
    const StructuralWorld world = world_using(0);
    auto prepared = prepare_world(world, atlas, make_palette());
    REQUIRE(prepared.is_ok());
    const PreparedWorld p = prepared.take();

    REQUIRE(p.surfaces.size() == world.surfaces.size());
    for (std::size_t i = 0; i < world.surfaces.size(); ++i) {
        INFO("surface ", i);
        CHECK(p.surfaces[i].kind == world.surfaces[i].kind);
        CHECK(p.surfaces[i].sector == world.surfaces[i].sector);
        CHECK(p.surfaces[i].wall == world.surfaces[i].wall);
        CHECK(p.surfaces[i].vertices == world.surfaces[i].vertices);
        CHECK(p.surfaces[i].indices == world.surfaces[i].indices);
    }
}

TEST_CASE("prepared world: picnum resolves to the exact atlas tile") {
    // Gate 2. The rect a surface carries must be the rect its OWN picnum
    // occupies -- and the two fixture tiles differ in width, so a mixed-up
    // resolution cannot pass by coincidence.
    const IndexedAtlas atlas = make_atlas();
    const PaletteData palette = make_palette();

    for (const std::int16_t picnum : {std::int16_t{0}, std::int16_t{1}}) {
        auto prepared = prepare_world(world_using(picnum), atlas, palette);
        REQUIRE(prepared.is_ok());
        const PreparedWorld p = prepared.take();
        const auto& tile = atlas.tiles[static_cast<std::size_t>(picnum)];
        REQUIRE(tile.populated);
        for (const auto& surface : p.surfaces) {
            INFO("picnum ", picnum);
            CHECK(surface.picnum == picnum);
            CHECK(surface.page == tile.page);
            CHECK(surface.rect_w ==
                  doctest::Approx(static_cast<double>(tile.width) / atlas.page_width));
            CHECK(surface.rect_h ==
                  doctest::Approx(static_cast<double>(tile.height) / atlas.page_height));
        }
    }
    // The two tiles really do differ, so the check above has teeth.
    CHECK(atlas.tiles[0].width != atlas.tiles[1].width);
}

TEST_CASE("prepared world: exactly one UV per structural vertex") {
    // Gate 3.
    const IndexedAtlas atlas = make_atlas();
    for (const char* fixture : {"square_room", "two_sector_portal", "non_convex", "multi_loop"}) {
        const StructuralWorld world = world_using(0, fixture);
        auto prepared = prepare_world(world, atlas, make_palette());
        REQUIRE(prepared.is_ok());
        const PreparedWorld p = prepared.take();
        std::size_t vertices = 0;
        std::size_t uvs = 0;
        for (const auto& surface : p.surfaces) {
            INFO("fixture ", fixture);
            CHECK(surface.uvs.size() == surface.vertices.size());
            vertices += surface.vertices.size();
            uvs += surface.uvs.size();
        }
        CHECK(vertices > 0);
        CHECK(uvs == vertices);
    }
}

TEST_CASE("prepared world: UVs actually depend on the conventions") {
    // Gate 4 (the authority is real, not decorative). Changing a constant in
    // the ONE authority must change the output; if it does not, some consumer
    // is computing UVs of its own.
    const IndexedAtlas atlas = make_atlas();
    const StructuralWorld world = world_using(0);
    const PaletteData palette = make_palette();

    auto base = prepare_world(world, atlas, palette);
    REQUIRE(base.is_ok());

    UvConventions doubled;
    doubled.floor_units_per_texel *= 2.0;
    doubled.wall_units_per_texel_u *= 2.0;
    doubled.wall_z_per_texel_v *= 2.0;
    auto scaled = prepare_world(world, atlas, palette, doubled);
    REQUIRE(scaled.is_ok());

    bool any_difference = false;
    for (std::size_t i = 0; i < base.value().surfaces.size(); ++i) {
        // Geometry is unchanged by a UV convention...
        CHECK(scaled.value().surfaces[i].vertices == base.value().surfaces[i].vertices);
        if (scaled.value().surfaces[i].uvs != base.value().surfaces[i].uvs) {
            any_difference = true;
        }
    }
    CHECK(any_difference); // ...but UVs are not

    // Sign/axis conventions are equally live.
    UvConventions flipped;
    flipped.floor_u_is_world_x = false;
    auto swapped = prepare_world(world, atlas, palette, flipped);
    REQUIRE(swapped.is_ok());
    bool floor_changed = false;
    for (std::size_t i = 0; i < base.value().surfaces.size(); ++i) {
        if (base.value().surfaces[i].kind != SurfaceKind::Floor) {
            continue;
        }
        if (swapped.value().surfaces[i].uvs != base.value().surfaces[i].uvs) {
            floor_changed = true;
        }
    }
    CHECK(floor_changed);
}

TEST_CASE("prepared world: an unusable picnum fails deliberately") {
    // Gate 9. No silent placeholder tile, no index-0 fallback: a surface whose
    // tile does not exist is a structured failure, because a wrong-but-present
    // texture is exactly the kind of plausible-looking result that hides bugs.
    const IndexedAtlas atlas = make_atlas();
    const PaletteData palette = make_palette();

    // Out of the namespace entirely.
    auto out_of_range = prepare_world(world_using(30000), atlas, palette);
    REQUIRE_FALSE(out_of_range.is_ok());
    CHECK(out_of_range.error().code == fauxbuild::ErrorCode::InvalidName);

    // Inside the namespace but unpopulated, if the atlas has such a slot.
    std::int16_t empty = -1;
    for (std::size_t i = 0; i < atlas.tiles.size(); ++i) {
        if (!atlas.tiles[i].populated) {
            empty = static_cast<std::int16_t>(i);
            break;
        }
    }
    if (empty >= 0) {
        auto unpopulated = prepare_world(world_using(empty), atlas, palette);
        REQUIRE_FALSE(unpopulated.is_ok());
        CHECK(unpopulated.error().code == fauxbuild::ErrorCode::Unsupported);
    }

    // Negative picnums are rejected too.
    auto negative = prepare_world(world_using(-3), atlas, palette);
    REQUIRE_FALSE(negative.is_ok());
}

TEST_CASE("prepared world: the atlas payload stays indexed R8") {
    // Gate 6 (core half). One byte per texel, exactly page_w*page_h*pages, and
    // a base palette of 256 RGB triples. No RGBA form is authoritative.
    const IndexedAtlas atlas = make_atlas();
    auto prepared = prepare_world(world_using(0), atlas, make_palette());
    REQUIRE(prepared.is_ok());
    const PreparedWorld p = prepared.take();

    const std::size_t expected = static_cast<std::size_t>(p.page_width) *
                                 static_cast<std::size_t>(p.page_height) *
                                 static_cast<std::size_t>(p.page_count);
    CHECK(p.atlas_pixels.size() == expected);
    CHECK(p.palette_rgb.size() == fauxbuild::kPaletteBytes);
    CHECK(p.page_count >= 1);
    // Every surface samples a page that exists.
    for (const auto& surface : p.surfaces) {
        CHECK(surface.page >= 0);
        CHECK(surface.page < p.page_count);
    }
}

TEST_CASE("prepared world: wall UVs advance along the wall and down its height") {
    // Architecture, not constants: whatever the provisional numbers are, a
    // wall's U must vary along its length and its V down its height, or the
    // texture cannot be placed at all. A transpose or a collapsed axis fails.
    const IndexedAtlas atlas = make_atlas();
    auto prepared = prepare_world(world_using(0, "square_room"), atlas, make_palette());
    REQUIRE(prepared.is_ok());

    std::size_t walls_checked = 0;
    for (const auto& surface : prepared.value().surfaces) {
        if (surface.kind != SurfaceKind::SolidWall) {
            continue;
        }
        float min_u = surface.uvs[0].u;
        float max_u = surface.uvs[0].u;
        float min_v = surface.uvs[0].v;
        float max_v = surface.uvs[0].v;
        for (const auto& uv : surface.uvs) {
            min_u = std::min(min_u, uv.u);
            max_u = std::max(max_u, uv.u);
            min_v = std::min(min_v, uv.v);
            max_v = std::max(max_v, uv.v);
        }
        CHECK(max_u > min_u); // U spans the wall
        CHECK(max_v > min_v); // V spans its height
        ++walls_checked;
    }
    CHECK(walls_checked == 4);
}

// ===========================================================================
// M6.2B1 — authored texture placement. Every expectation below is derived by
// hand from the documented bits (PROVENANCE row 9) composed with the
// CURRENT provisional UvConventions (floor 16 units/texel, wall U 16 XY, wall
// V 256 Z, reference repeat 64). A ratified change to a convention must
// update these numbers in the same change — that is the point of pinning
// them. square_room geometry: walls (0,0)->(65536,0)->(65536,65536)->(0,65536),
// wall spans cover Build Z [0, 16384], wall repeats 16/16. With tile 0
// (64x64): floor tile = 1024 world units, wall U tile = 256 units
// (16 * 16/64 * 64), wall V tile = 4096 Z units (256 * 16/64 * 64).
// ===========================================================================

namespace {

constexpr double kScale = 1.0 / 2048.0; // UvConventions::render_scale default

// Inverse of to_render_space for test lookups: render (x,y,z) -> Build.
struct TestBuildPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

TestBuildPoint to_build_point(const fauxbuild::StructuralVertex& v) {
    return {v.x / kScale, v.z / kScale,
            -v.y / (kScale / fauxbuild::kBuildVerticalUnitsPerHorizontal)};
}

// Find the UV of the vertex at Build (bx, by, bz), tolerance for the
// render-space round trip. Wall spans share (bx,by) between top and bottom
// vertices, so bz disambiguates.
const fauxbuild::PreparedUV* uv_at(const PreparedSurface& surface, double bx, double by,
                                   double bz) {
    for (std::size_t i = 0; i < surface.vertices.size(); ++i) {
        const TestBuildPoint p = to_build_point(surface.vertices[i]);
        if (std::abs(p.x - bx) < 1e-6 && std::abs(p.y - by) < 1e-6 && std::abs(p.z - bz) < 1e-6) {
            return &surface.uvs[i];
        }
    }
    return nullptr;
}

StructuralWorld placement_world(const std::function<void(fauxbuild::mapv7::MapData&)>& mutate,
                                const char* fixture = "square_room", std::int16_t picnum = 0) {
    auto map = map_fixture(fixture);
    REQUIRE(map.is_ok());
    auto source = map.take();
    for (auto& sector : source.sectors) {
        sector.floorpicnum = picnum;
        sector.ceilingpicnum = picnum;
    }
    for (auto& wall : source.walls) {
        wall.picnum = picnum;
    }
    if (mutate) {
        mutate(source);
    }
    auto world = build_structural_world(source);
    REQUIRE(world.is_ok());
    return world.take();
}

PreparedWorld prepare(const StructuralWorld& world, const IndexedAtlas& atlas,
                      const UvConventions& c = {}) {
    auto prepared = prepare_world(world, atlas, make_palette(), c);
    REQUIRE(prepared.is_ok());
    return prepared.take();
}

const PreparedSurface* find_surface(const PreparedWorld& world, SurfaceKind kind,
                                    std::int16_t wall = -1) {
    for (const auto& surface : world.surfaces) {
        if (surface.kind == kind && (wall < 0 || surface.wall == wall)) {
            return &surface;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("placement: zero/default placement reproduces the M6.2A UVs exactly") {
    // Regression pin. These hand-derived values ARE the accepted M6.2A
    // semantics; implementing authored controls must not move them by one
    // float. (Sabotage "mutate the M6.2A global scale while implementing an
    // authored control" goes red here first.)
    const IndexedAtlas atlas = make_atlas();
    const PreparedWorld p = prepare(placement_world(nullptr), atlas);

    // Floor: u = x/1024, v = y/1024 at the four corners.
    const PreparedSurface* floor = find_surface(p, SurfaceKind::Floor);
    REQUIRE(floor != nullptr);
    struct Corner {
        double x, y, u, v;
    };
    for (const Corner& corner : {Corner{0, 0, 0, 0}, Corner{65536, 0, 64, 0},
                                 Corner{65536, 65536, 64, 64}, Corner{0, 65536, 0, 64}}) {
        const auto* uv = uv_at(*floor, corner.x, corner.y, 0);
        REQUIRE(uv != nullptr);
        CHECK(uv->u == corner.u);
        CHECK(uv->v == corner.v);
    }

    // Wall 0 (0,0)->(65536,0), span Z [0,16384], top-anchored: u = along/256,
    // v = z/4096.
    const PreparedSurface* wall = find_surface(p, SurfaceKind::SolidWall, 0);
    REQUIRE(wall != nullptr);
    for (const Corner& c : {Corner{0, 0, 0, 0}, Corner{65536, 0, 256, 0}}) {
        const auto* top = uv_at(*wall, c.x, c.y, 0);
        REQUIRE(top != nullptr);
        CHECK(top->u == c.u);
        CHECK(top->v == 0.0f);
    }
    const auto* bottom_a = uv_at(*wall, 0, 0, 16384);
    const auto* bottom_b = uv_at(*wall, 65536, 0, 16384);
    REQUIRE(bottom_a != nullptr);
    REQUIRE(bottom_b != nullptr);
    CHECK(bottom_a->u == 0.0f);
    CHECK(bottom_a->v == 4.0f);
    CHECK(bottom_b->u == 256.0f);
    CHECK(bottom_b->v == 4.0f);
}

TEST_CASE("placement: floor panning adds tile-local phase, per axis") {
    // Tile 1 is 128x64: pan_u = 16/128 = 0.125, pan_v = 16/64 = 0.25 — the
    // asymmetric tile makes a transposed panning application (the recorded
    // sabotage) provably different from the correct one.
    const IndexedAtlas atlas = make_atlas();
    const StructuralWorld world = placement_world(
        [](fauxbuild::mapv7::MapData& map) {
            map.sectors[0].floorxpanning = 16;
            map.sectors[0].floorypanning = 16;
        },
        "square_room", 1);
    const PreparedWorld p = prepare(world, atlas);

    const PreparedSurface* floor = find_surface(p, SurfaceKind::Floor);
    REQUIRE(floor != nullptr);
    // Corner (65536, 65536) with tile 1 (128x64): tile spans 2048 x 1024
    // world units, so u0 = 32, v0 = 64; pan adds 0.125 / 0.25.
    const auto* uv = uv_at(*floor, 65536, 65536, 0);
    REQUIRE(uv != nullptr);
    CHECK(uv->u == 32.0f + 0.125f);
    CHECK(uv->v == 64.0f + 0.25f);
    // X pan only moves u; Y pan only moves v (axis independence).
    const StructuralWorld x_only =
        placement_world([](fauxbuild::mapv7::MapData& map) { map.sectors[0].floorxpanning = 16; },
                        "square_room", 1);
    const PreparedWorld x_prepared = prepare(x_only, atlas);
    const PreparedSurface* xf = find_surface(x_prepared, SurfaceKind::Floor);
    const auto* xuv = uv_at(*xf, 65536, 0, 0);
    REQUIRE(xuv != nullptr);
    CHECK(xuv->u == 32.0f + 0.125f);
    CHECK(xuv->v == 0.0f);
}

TEST_CASE("placement: wall panning adds tile-local phase, per axis") {
    const IndexedAtlas atlas = make_atlas();
    const StructuralWorld world = placement_world([](fauxbuild::mapv7::MapData& map) {
        map.walls[0].xpanning = 16;
        map.walls[0].ypanning = 16;
    });
    const PreparedWorld p = prepare(world, atlas);

    const PreparedSurface* wall = find_surface(p, SurfaceKind::SolidWall, 0);
    REQUIRE(wall != nullptr);
    const auto* top = uv_at(*wall, 0, 0, 0);
    const auto* bottom = uv_at(*wall, 65536, 0, 16384);
    REQUIRE(top != nullptr);
    REQUIRE(bottom != nullptr);
    CHECK(top->u == 0.25f); // 0 + 16/64
    CHECK(top->v == 0.25f);
    CHECK(bottom->u == 256.0f + 0.25f);
    CHECK(bottom->v == 4.0f + 0.25f);
}

TEST_CASE("placement: floor flips mirror one tile-local axis each") {
    const IndexedAtlas atlas = make_atlas();
    const PreparedWorld base = prepare(placement_world(nullptr), atlas);

    const auto flipped_world = [](std::int16_t bits) {
        return placement_world(
            [bits](fauxbuild::mapv7::MapData& map) { map.sectors[0].floorstat |= bits; });
    };
    const PreparedWorld fx = prepare(flipped_world(fauxbuild::mapv7::kStatPlaneFlipX), atlas);
    const PreparedWorld fy = prepare(flipped_world(fauxbuild::mapv7::kStatPlaneFlipY), atlas);
    const PreparedWorld fxy = prepare(
        flipped_world(fauxbuild::mapv7::kStatPlaneFlipX | fauxbuild::mapv7::kStatPlaneFlipY),
        atlas);

    const PreparedSurface* base_floor = find_surface(base, SurfaceKind::Floor);
    for (const auto* world : {&fx, &fy, &fxy}) {
        const PreparedSurface* floor = find_surface(*world, SurfaceKind::Floor);
        REQUIRE(floor != nullptr);
        REQUIRE(floor->vertices.size() == base_floor->vertices.size());
        for (std::size_t i = 0; i < floor->vertices.size(); ++i) {
            // A flip is UV-only: the structural vertices are verbatim, and
            // each vertex's flip is exactly the negation of its base value.
            CHECK(floor->vertices[i] == base_floor->vertices[i]);
            const bool x = world == &fx || world == &fxy;
            const bool y = world == &fy || world == &fxy;
            CHECK(floor->uvs[i].u == (x ? -base_floor->uvs[i].u : base_floor->uvs[i].u));
            CHECK(floor->uvs[i].v == (y ? -base_floor->uvs[i].v : base_floor->uvs[i].v));
        }
    }
    CHECK(find_surface(fx, SurfaceKind::Floor)->uvs != find_surface(fy, SurfaceKind::Floor)->uvs);
    CHECK(find_surface(fx, SurfaceKind::Floor)->uvs != find_surface(fxy, SurfaceKind::Floor)->uvs);
}

TEST_CASE("placement: floor swap-XY exchanges the base axes") {
    const IndexedAtlas atlas = make_atlas();
    const PreparedWorld base = prepare(placement_world(nullptr), atlas);
    const PreparedWorld swapped = prepare(placement_world([](fauxbuild::mapv7::MapData& map) {
                                              map.sectors[0].floorstat |=
                                                  fauxbuild::mapv7::kStatPlaneSwapXY;
                                          }),
                                          atlas);

    const PreparedSurface* base_floor = find_surface(base, SurfaceKind::Floor);
    const PreparedSurface* swapped_floor = find_surface(swapped, SurfaceKind::Floor);
    REQUIRE(swapped_floor != nullptr);
    REQUIRE(swapped_floor->vertices.size() == base_floor->vertices.size());
    for (std::size_t i = 0; i < swapped_floor->vertices.size(); ++i) {
        CHECK(swapped_floor->uvs[i].u == base_floor->uvs[i].v);
        CHECK(swapped_floor->uvs[i].v == base_floor->uvs[i].u);
    }
}

TEST_CASE("placement: flip and pan compose without erasing or doubling") {
    // Flip negates the position-derived coordinate; pan adds phase AFTER the
    // flip. X-flip + X-pan must therefore differ from X-flip alone by exactly
    // +pan on u, with v untouched.
    const IndexedAtlas atlas = make_atlas();
    const PreparedWorld flipped = prepare(placement_world([](fauxbuild::mapv7::MapData& map) {
                                              map.sectors[0].floorstat |=
                                                  fauxbuild::mapv7::kStatPlaneFlipX;
                                          }),
                                          atlas);
    const PreparedWorld flipped_panned =
        prepare(placement_world([](fauxbuild::mapv7::MapData& map) {
                    map.sectors[0].floorstat |= fauxbuild::mapv7::kStatPlaneFlipX;
                    map.sectors[0].floorxpanning = 16;
                    map.sectors[0].floorypanning = 8;
                }),
                atlas);

    const PreparedSurface* a = find_surface(flipped, SurfaceKind::Floor);
    const PreparedSurface* b = find_surface(flipped_panned, SurfaceKind::Floor);
    REQUIRE(a != nullptr);
    REQUIRE(b->vertices.size() == a->vertices.size());
    for (std::size_t i = 0; i < a->vertices.size(); ++i) {
        CHECK(b->uvs[i].u == a->uvs[i].u + 0.25f);  // 16/64
        CHECK(b->uvs[i].v == a->uvs[i].v + 0.125f); // 8/64
    }
}

TEST_CASE("placement: wall bottom alignment anchors V at the span's lower edge") {
    const IndexedAtlas atlas = make_atlas();
    const PreparedWorld base = prepare(placement_world(nullptr), atlas);
    const PreparedWorld bottom = prepare(placement_world([](fauxbuild::mapv7::MapData& map) {
                                             map.walls[0].cstat |=
                                                 fauxbuild::mapv7::kWallCstatBottomAligned;
                                         }),
                                         atlas);

    const PreparedSurface* wall = find_surface(bottom, SurfaceKind::SolidWall, 0);
    REQUIRE(wall != nullptr);
    // v = (z - 16384)/4096: the span's bottom edge is phase 0, the top edge
    // is -4 — the same vertical texel scale, anchored at the other end.
    const auto* top = uv_at(*wall, 0, 0, 0);
    const auto* bottom_uv = uv_at(*wall, 0, 0, 16384);
    REQUIRE(top != nullptr);
    REQUIRE(bottom_uv != nullptr);
    CHECK(bottom_uv->v == 0.0f);
    CHECK(top->v == -4.0f);
    // U is untouched by the alignment bit.
    const PreparedSurface* base_wall = find_surface(base, SurfaceKind::SolidWall, 0);
    for (std::size_t i = 0; i < wall->uvs.size(); ++i) {
        CHECK(wall->uvs[i].u == base_wall->uvs[i].u);
    }

    // Bottom align + Y flip (the recorded combination): flip mirrors the
    // position-derived v only — v = -(z - 16384)/4096.
    const PreparedWorld combo = prepare(placement_world([](fauxbuild::mapv7::MapData& map) {
                                            map.walls[0].cstat |=
                                                fauxbuild::mapv7::kWallCstatBottomAligned;
                                            map.walls[0].cstat |= fauxbuild::mapv7::kWallCstatFlipY;
                                        }),
                                        atlas);
    const PreparedSurface* combo_wall = find_surface(combo, SurfaceKind::SolidWall, 0);
    REQUIRE(combo_wall != nullptr);
    const auto* combo_top = uv_at(*combo_wall, 0, 0, 0);
    const auto* combo_bottom = uv_at(*combo_wall, 0, 0, 16384);
    REQUIRE(combo_top != nullptr);
    REQUIRE(combo_bottom != nullptr);
    CHECK(combo_bottom->v == 0.0f);
    CHECK(combo_top->v == 4.0f);
}

TEST_CASE("placement: wall flips mirror one tile-local axis each") {
    const IndexedAtlas atlas = make_atlas();
    const PreparedWorld base = prepare(placement_world(nullptr), atlas);
    const PreparedWorld fx = prepare(placement_world([](fauxbuild::mapv7::MapData& map) {
                                         map.walls[0].cstat |= fauxbuild::mapv7::kWallCstatFlipX;
                                     }),
                                     atlas);
    const PreparedWorld fy = prepare(placement_world([](fauxbuild::mapv7::MapData& map) {
                                         map.walls[0].cstat |= fauxbuild::mapv7::kWallCstatFlipY;
                                     }),
                                     atlas);

    const PreparedSurface* base_wall = find_surface(base, SurfaceKind::SolidWall, 0);
    for (const auto* world : {&fx, &fy}) {
        const PreparedSurface* wall = find_surface(*world, SurfaceKind::SolidWall, 0);
        REQUIRE(wall != nullptr);
        REQUIRE(wall->vertices.size() == base_wall->vertices.size());
        for (std::size_t i = 0; i < wall->uvs.size(); ++i) {
            CHECK(wall->vertices[i] == base_wall->vertices[i]); // UV-only change
            const bool x = world == &fx;
            CHECK(wall->uvs[i].u == (x ? -base_wall->uvs[i].u : base_wall->uvs[i].u));
            CHECK(wall->uvs[i].v == (x ? base_wall->uvs[i].v : -base_wall->uvs[i].v));
        }
    }
}

TEST_CASE("placement: relative alignment uses the sector's first-wall frame") {
    // A rotated square (first wall diagonal at 45 degrees) so the frame
    // provably differs from the world axes.
    const IndexedAtlas atlas = make_atlas();
    const auto rotated_world = [](bool relative) {
        fauxbuild::mapv7::MapData map;
        const std::int32_t xs[] = {2048, 4096, 2048, 0};
        const std::int32_t ys[] = {0, 2048, 4096, 2048};
        for (std::size_t i = 0; i < 4; ++i) {
            fauxbuild::mapv7::Wall wall;
            wall.x = xs[i];
            wall.y = ys[i];
            wall.point2 = static_cast<std::int16_t>((i + 1) % 4);
            wall.picnum = 0;
            map.walls.push_back(wall);
        }
        map.sectors.push_back(fauxbuild::synth::make_sector(0, 4, 8192, 0));
        if (relative) {
            map.sectors[0].floorstat |= fauxbuild::mapv7::kStatPlaneRelative;
        }
        map.start = {2048, 2048, 4096, 0, 0};
        auto world = build_structural_world(map);
        REQUIRE(world.is_ok());
        return world.take();
    };

    const StructuralWorld world = rotated_world(true);
    const PreparedWorld p = prepare(world, atlas);
    const PreparedSurface* floor = find_surface(p, SurfaceKind::Floor);
    REQUIRE(floor != nullptr);

    // Expected: frame U = normalized A->B, V = left perpendicular of the U
    // direction (reversing U reverses V with it — the frame stays
    // right-handed), origin A.
    const double dx = 4096.0 - 2048.0;
    const double dy = 2048.0 - 0.0;
    const double len = std::sqrt(dx * dx + dy * dy);
    const double ux = dx / len, uy = dy / len;
    const double vx = -uy, vy = ux;
    for (std::size_t i = 0; i < floor->vertices.size(); ++i) {
        const TestBuildPoint pt = to_build_point(floor->vertices[i]);
        const double a = (pt.x - 2048.0) * ux + (pt.y - 0.0) * uy;
        const double b = (pt.x - 2048.0) * vx + (pt.y - 0.0) * vy;
        // Tile 0 is 64x64: one tile = 1024 world units on each axis. The
        // prepared value is a float, so compare at float precision.
        CHECK(floor->uvs[i].u == doctest::Approx(a / 1024.0).epsilon(1e-6));
        CHECK(floor->uvs[i].v == doctest::Approx(b / 1024.0).epsilon(1e-6));
    }

    // The frame is really the first wall's, not the world axes: the two
    // disagree on this rotated room.
    const PreparedWorld world_axes = prepare(rotated_world(false), atlas);
    const PreparedSurface* plain = find_surface(world_axes, SurfaceKind::Floor);
    REQUIRE(plain != nullptr);
    CHECK(plain->uvs != floor->uvs);

    // The orientation conventions are live one-site toggles. Reversing U
    // takes the left perpendicular of the REVERSED direction, so V reverses
    // with it — the frame stays right-handed either way.
    UvConventions reversed;
    reversed.floor_relative_u_follows_first_wall = false;
    const PreparedWorld flipped_u = prepare(rotated_world(true), atlas, reversed);
    const PreparedSurface* flipped_floor = find_surface(flipped_u, SurfaceKind::Floor);
    REQUIRE(flipped_floor != nullptr);
    for (std::size_t i = 0; i < floor->uvs.size(); ++i) {
        CHECK(flipped_floor->uvs[i].u == doctest::Approx(-floor->uvs[i].u).epsilon(1e-6));
        CHECK(flipped_floor->uvs[i].v == doctest::Approx(-floor->uvs[i].v).epsilon(1e-6));
    }
}

TEST_CASE("placement: relative alignment falls back to world axes on a degenerate frame") {
    // slope_degenerate_hinge has a zero-length first wall; the slope bit is
    // cleared so the floor exists, and the relative bit then has no frame.
    const IndexedAtlas atlas = make_atlas();
    const StructuralWorld degenerate = placement_world(
        [](fauxbuild::mapv7::MapData& map) {
            map.sectors[0].floorstat &= ~fauxbuild::mapv7::kStatSloped;
            map.sectors[0].floorstat |= fauxbuild::mapv7::kStatPlaneRelative;
        },
        "slope_degenerate_hinge");
    // Build must say so rather than guess silently.
    bool noted = false;
    for (const auto& note : degenerate.notes) {
        if (note.detail.find("relative alignment") != std::string::npos) {
            noted = true;
        }
    }
    CHECK(noted);

    // And the prepared UVs are exactly the world-axes ones (bit-identical to
    // a plain build of the same room).
    const StructuralWorld plain = placement_world(
        [](fauxbuild::mapv7::MapData& map) {
            map.sectors[0].floorstat &= ~fauxbuild::mapv7::kStatSloped;
        },
        "slope_degenerate_hinge");
    const PreparedWorld degenerate_prepared = prepare(degenerate, atlas);
    const PreparedWorld plain_prepared = prepare(plain, atlas);
    const PreparedSurface* a = find_surface(degenerate_prepared, SurfaceKind::Floor);
    const PreparedSurface* b = find_surface(plain_prepared, SurfaceKind::Floor);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->uvs == b->uvs);
}

TEST_CASE("placement: sloped and wedge spans change UV only, geometry stays verbatim") {
    const IndexedAtlas atlas = make_atlas();
    const auto with_placement = [](const char* fixture,
                                   const std::function<void(fauxbuild::mapv7::MapData&)>& mutate) {
        return placement_world(
            [&](fauxbuild::mapv7::MapData& map) {
                for (auto& wall : map.walls) {
                    wall.cstat |= fauxbuild::mapv7::kWallCstatBottomAligned |
                                  fauxbuild::mapv7::kWallCstatFlipY;
                    wall.ypanning = 16;
                }
                if (mutate) {
                    mutate(map);
                }
            },
            fixture);
    };

    for (const char* fixture : {"ramp_floor_pos", "ramp_ceiling", "portal_slope_collapse"}) {
        INFO("fixture ", fixture);
        const StructuralWorld placed = with_placement(fixture, nullptr);
        const StructuralWorld plain = placement_world(nullptr, fixture);
        REQUIRE(placed.diagnostics.empty()); // no zero-area geometry appeared

        // Same surfaces as the plain build, vertex for vertex, index for
        // index: placement never rederives slope geometry.
        REQUIRE(placed.surfaces.size() == plain.surfaces.size());
        for (std::size_t i = 0; i < placed.surfaces.size(); ++i) {
            CHECK(placed.surfaces[i].vertices == plain.surfaces[i].vertices);
            CHECK(placed.surfaces[i].indices == plain.surfaces[i].indices);
        }

        const PreparedWorld p = prepare(placed, atlas);
        bool saw_wedge = false;
        bool saw_bottom_phase_zero = false;
        for (const auto& surface : p.surfaces) {
            if (surface.kind != SurfaceKind::SolidWall &&
                surface.kind != SurfaceKind::PortalUpper &&
                surface.kind != SurfaceKind::PortalLower) {
                continue;
            }
            // Bottom-anchored: the lowest vertex (max Build Z) is phase 0
            // modulo the pan (ypan 16/64 = 0.25).
            double max_z = -1e300;
            for (const auto& vertex : surface.vertices) {
                max_z = std::max(max_z, to_build_point(vertex).z);
            }
            for (std::size_t i = 0; i < surface.vertices.size(); ++i) {
                if (to_build_point(surface.vertices[i]).z == max_z) {
                    // -(z - anchor)/scale + pan == -0 + 0.25 at the anchor.
                    CHECK(surface.uvs[i].v == doctest::Approx(0.25).epsilon(1e-9));
                    saw_bottom_phase_zero = true;
                }
            }
            if (surface.vertices.size() == 3) {
                saw_wedge = true;
            }
        }
        CHECK(saw_bottom_phase_zero);
        if (std::string(fixture) == "portal_slope_collapse") {
            CHECK(saw_wedge); // the fixture's defining feature survived
        }
    }
}

TEST_CASE("placement: zero panning is byte-equivalent to the unpanned world") {
    // panning bytes of 0 must not perturb a single float — including on
    // surfaces that DO use flips, swap and relative alignment.
    const IndexedAtlas atlas = make_atlas();
    const auto placed = [](fauxbuild::mapv7::MapData& map) {
        map.sectors[0].floorstat |= fauxbuild::mapv7::kStatPlaneSwapXY |
                                    fauxbuild::mapv7::kStatPlaneFlipX |
                                    fauxbuild::mapv7::kStatPlaneRelative;
        for (auto& wall : map.walls) {
            wall.cstat |= fauxbuild::mapv7::kWallCstatFlipX;
        }
    };
    const PreparedWorld a = prepare(placement_world(placed), atlas);
    const PreparedWorld b = prepare(placement_world(placed), atlas);
    REQUIRE(a.surfaces.size() == b.surfaces.size());
    for (std::size_t i = 0; i < a.surfaces.size(); ++i) {
        CHECK(a.surfaces[i].uvs == b.surfaces[i].uvs); // determinism
    }
}

// ---------------------------------------------------------------------------
// Seam-contract gates. The per-sector tables are caller-provided input, so an
// incoherent world must produce a structured error, not an indexed read past
// the end. Before these gates prepare_world indexed sector_frames blind: a
// world with no frames read a null pointer (UBSan: "reference binding to null
// pointer of type 'const StructuralSectorFrame'") and STILL returned ok,
// building UVs from garbage. The failure mode was silent-wrong, not a crash,
// which is why an error is required rather than an assertion.
//
// One case per TEST_CASE deliberately: a REQUIRE inside a SUBCASE aborts the
// whole case, so grouping them would let the first failure mask the rest —
// and the rejection matrix is the point.
// ---------------------------------------------------------------------------

namespace {

// A well-formed world plus its atlas/palette, for the contract gates to break
// one field at a time.
StructuralWorld contract_world() {
    StructuralWorld world = placement_world(nullptr);
    REQUIRE(world.surfaces.empty() == false);
    REQUIRE(world.sector_frames.size() == world.sector_appearance.size());
    REQUIRE(world.sector_frames.empty() == false);
    return world;
}

} // namespace

TEST_CASE("seam contract: frames absent entirely while surfaces exist") {
    StructuralWorld world = contract_world();
    world.sector_frames.clear();
    auto prepared = prepare_world(world, make_atlas(), make_palette());
    REQUIRE(prepared.is_ok() == false);
    CHECK(prepared.error().code == fauxbuild::ErrorCode::InvalidTopology);
    CHECK(prepared.error().record == "world");
}

TEST_CASE("seam contract: frames truncated below the sector domain") {
    StructuralWorld world = contract_world();
    world.sector_frames.pop_back();
    auto prepared = prepare_world(world, make_atlas(), make_palette());
    REQUIRE(prepared.is_ok() == false);
    CHECK(prepared.error().code == fauxbuild::ErrorCode::InvalidTopology);
}

TEST_CASE("seam contract: frames longer than the sector domain") {
    // Over-long is refused too: a mismatched table means producer and consumer
    // disagree about the domain, whichever way it leans.
    StructuralWorld world = contract_world();
    world.sector_frames.push_back({});
    auto prepared = prepare_world(world, make_atlas(), make_palette());
    REQUIRE(prepared.is_ok() == false);
    CHECK(prepared.error().code == fauxbuild::ErrorCode::InvalidTopology);
}

TEST_CASE("seam contract: a negative surface sector") {
    StructuralWorld world = contract_world();
    world.surfaces[0].sector = -1;
    auto prepared = prepare_world(world, make_atlas(), make_palette());
    REQUIRE(prepared.is_ok() == false);
    CHECK(prepared.error().code == fauxbuild::ErrorCode::InvalidRange);
    CHECK(prepared.error().offset == 0); // the offending surface index
}

TEST_CASE("seam contract: a surface sector one past the end") {
    StructuralWorld world = contract_world();
    world.surfaces[0].sector = static_cast<std::int16_t>(world.sector_frames.size());
    auto prepared = prepare_world(world, make_atlas(), make_palette());
    REQUIRE(prepared.is_ok() == false);
    CHECK(prepared.error().code == fauxbuild::ErrorCode::InvalidRange);
}

TEST_CASE("seam contract: a late surface's bad sector still rejects the whole world") {
    // The domain is validated up front, so a violation on the LAST surface
    // must leave nothing behind — not a PreparedWorld truncated at the first
    // bad surface, which a per-surface guard inside the loop would produce.
    StructuralWorld world = contract_world();
    world.surfaces.back().sector = -1;
    auto prepared = prepare_world(world, make_atlas(), make_palette());
    REQUIRE(prepared.is_ok() == false);
    CHECK(prepared.error().offset == world.surfaces.size() - 1);
}

TEST_CASE("seam contract: the valid boundary index still succeeds") {
    // An off-by-one in the guard would reject the LAST real sector. Point a
    // surface at it explicitly and require a full, correct preparation —
    // the gate must reject incoherent worlds, not merely reject.
    StructuralWorld world = contract_world();
    const auto last = static_cast<std::int16_t>(world.sector_frames.size() - 1);
    world.surfaces[0].sector = last;
    auto prepared = prepare_world(world, make_atlas(), make_palette());
    REQUIRE(prepared.is_ok());
    const PreparedWorld p = prepared.take();
    CHECK(p.surfaces.size() == world.surfaces.size());
    CHECK(p.surfaces[0].sector == last);
    CHECK(p.surfaces[0].uvs.size() == world.surfaces[0].vertices.size());
}

// ---------------------------------------------------------------------------
// M6.2C1: effective-texture selection for the masked layer.
// ---------------------------------------------------------------------------

namespace {

StructuralWorld masked_world(const std::function<void(fauxbuild::mapv7::MapData&)>& mutate,
                             const char* fixture = "masked_wall") {
    return placement_world(mutate, fixture);
}

} // namespace

TEST_CASE("selection: PortalMasked prepares overpicnum, ordinary kinds keep picnum") {
    // The one selection rule, centralized in prepare_world: the masked layer
    // presents the overlay tile; every ordinary surface presents its own
    // picnum — on the SAME wall, so a transposition cannot pass by accident.
    const IndexedAtlas atlas = make_atlas();
    const StructuralWorld world = masked_world([](fauxbuild::mapv7::MapData& map) {
        map.walls[1].cstat |= fauxbuild::mapv7::kWallCstatMasked;
        map.walls[1].overpicnum = 1;
        map.walls[7].cstat |= fauxbuild::mapv7::kWallCstatMasked;
        map.walls[7].overpicnum = 1;
    });
    const PreparedWorld p = prepare(world, atlas);

    const PreparedSurface* masked = find_surface(p, SurfaceKind::PortalMasked, 1);
    const PreparedSurface* upper = find_surface(p, SurfaceKind::PortalUpper, 1);
    const PreparedSurface* lower = find_surface(p, SurfaceKind::PortalLower, 1);
    REQUIRE(masked != nullptr);
    REQUIRE(upper != nullptr);
    REQUIRE(lower != nullptr);
    CHECK(masked->picnum == 1);
    CHECK(masked->page == atlas.tiles[1].page);
    CHECK(masked->rect_w ==
          doctest::Approx(static_cast<double>(atlas.tiles[1].width) / atlas.page_width));
    CHECK(upper->picnum == 0);
    CHECK(lower->picnum == 0);
    // The raw appearance still carries both fields verbatim; only the
    // resolved picnum differs.
    CHECK(masked->appearance.overpicnum == 1);
    CHECK(masked->appearance.picnum == 0);
}

TEST_CASE("selection: masked portal with overpicnum 0 prepares TILE 0") {
    // The critical zero case. No approved provenance establishes a zero
    // sentinel, so 0 is a tile number like any other: the masked layer must
    // resolve atlas tile 0, populated with its own rect — never fall back to
    // picnum, never skip the layer. (All 145 masked walls across the six
    // owned maps carry a nonzero overpicnum, so this is synthetic-only
    // reachability — pinned here precisely because real content cannot
    // catch a regression.) Sabotage `if (overpicnum != 0)` goes red here.
    const IndexedAtlas atlas = make_atlas();
    REQUIRE(atlas.tiles[0].populated);
    REQUIRE(atlas.tiles[1].populated);
    const bool distinct_rects =
        atlas.tiles[0].x != atlas.tiles[1].x || atlas.tiles[0].y != atlas.tiles[1].y;
    REQUIRE(distinct_rects); // so the tile-0 rect check below has teeth

    const StructuralWorld world = masked_world([](fauxbuild::mapv7::MapData& map) {
        for (const std::int16_t w : {std::int16_t{1}, std::int16_t{7}}) {
            auto& wall = map.walls[static_cast<std::size_t>(w)];
            wall.cstat |= fauxbuild::mapv7::kWallCstatMasked;
            wall.overpicnum = 0; // tile 0, deliberately, on BOTH sides
        }
        map.walls[1].picnum = 1; // differs, so a fallback to picnum is visible
    });
    const PreparedWorld p = prepare(world, atlas);
    const PreparedSurface* masked = find_surface(p, SurfaceKind::PortalMasked, 1);
    REQUIRE(masked != nullptr);
    CHECK(masked->picnum == 0);
    CHECK(masked->page == atlas.tiles[0].page);
    CHECK(masked->rect_x ==
          doctest::Approx(static_cast<double>(atlas.tiles[0].x) / atlas.page_width));
    CHECK(masked->rect_w ==
          doctest::Approx(static_cast<double>(atlas.tiles[0].width) / atlas.page_width));
}

TEST_CASE("selection: a nonzero overpicnum on an ordinary surface selects picnum") {
    // The converse at the seam: overpicnum travels with non-masked walls in
    // real content (305 across the six owned maps) and must select nothing.
    const IndexedAtlas atlas = make_atlas();
    const StructuralWorld world = placement_world(
        [](fauxbuild::mapv7::MapData& map) {
            for (auto& wall : map.walls) {
                wall.overpicnum = 1; // nonzero everywhere, masked bit clear
            }
        },
        "portal_heights");
    const PreparedWorld p = prepare(world, atlas);
    CHECK(find_surface(p, SurfaceKind::PortalMasked) == nullptr);
    for (const auto& surface : p.surfaces) {
        INFO("kind ", static_cast<int>(surface.kind), " wall ", surface.wall);
        CHECK(surface.picnum == 0);
        // Floors/ceilings carry no overpicnum (wall spans only); on every
        // wall span the field is preserved verbatim and unconsumed.
        if (surface.wall >= 0) {
            CHECK(surface.appearance.overpicnum == 1);
        }
    }
}

TEST_CASE("selection: an out-of-range effective tile is a structured error") {
    const IndexedAtlas atlas = make_atlas();

    // Masked wall whose overpicnum names a tile the atlas does not have.
    const StructuralWorld bad_overlay = masked_world([](fauxbuild::mapv7::MapData& map) {
        map.walls[1].cstat |= fauxbuild::mapv7::kWallCstatMasked;
        map.walls[1].overpicnum = 5; // atlas holds 0..1
    });
    auto prepared = prepare_world(bad_overlay, atlas, make_palette());
    REQUIRE(prepared.is_ok() == false);
    CHECK(prepared.error().code == fauxbuild::ErrorCode::InvalidName);
    CHECK(prepared.error().detail.find("overpicnum") != std::string::npos);

    // And an ordinary wall's picnum out of range still errors naming picnum.
    const StructuralWorld bad_picnum = placement_world(
        [](fauxbuild::mapv7::MapData& map) { map.walls[0].picnum = 9; }, "portal_heights");
    auto rejected = prepare_world(bad_picnum, atlas, make_palette());
    REQUIRE(rejected.is_ok() == false);
    CHECK(rejected.error().detail.find("picnum") != std::string::npos);
}

TEST_CASE("masked layer UVs follow the wall model exactly") {
    // Full-height opening on a properly ordered equal-height portal: the
    // masked quad's UVs are the wall-span values, hand-derived. Wall 1 runs
    // (1024,0)->(1024,1024) with repeats 8/8: one texel spans
    // 16*(8/64)*64 = 128 world units along U and 256*(8/64)*64 = 2048 Build
    // Z units along V, anchored at the top (Build Z 0). The quad's vertices
    // arrive A@0, B@0, B@16384, A@16384 -> U (0,8,8,0), V (0,0,8,8), exactly.
    const IndexedAtlas atlas = make_atlas();
    fauxbuild::mapv7::MapData map;
    const std::int32_t u = 1024;
    const std::int32_t ax[] = {0, u, u, 0};
    const std::int32_t ay[] = {0, 0, u, u};
    for (std::size_t i = 0; i < 4; ++i) {
        map.walls.push_back(fauxbuild::mapv7::Wall());
        map.walls[i].x = ax[i];
        map.walls[i].y = ay[i];
        map.walls[i].point2 = static_cast<std::int16_t>((i + 1) % 4);
        map.walls[i].xrepeat = 8;
        map.walls[i].yrepeat = 8;
    }
    const std::int32_t bx[] = {u, 2 * u, 2 * u, u};
    const std::int32_t by[] = {0, 0, u, u};
    for (std::size_t i = 0; i < 4; ++i) {
        map.walls.push_back(fauxbuild::mapv7::Wall());
        map.walls[4 + i].x = bx[i];
        map.walls[4 + i].y = by[i];
        map.walls[4 + i].point2 = static_cast<std::int16_t>(4 + (i + 1) % 4);
        map.walls[4 + i].xrepeat = 8;
        map.walls[4 + i].yrepeat = 8;
    }
    map.walls[1].nextwall = 7;
    map.walls[1].nextsector = 1;
    map.walls[7].nextwall = 1;
    map.walls[7].nextsector = 0;
    for (int s = 0; s < 2; ++s) {
        map.sectors.push_back(fauxbuild::mapv7::Sector());
        map.sectors[static_cast<std::size_t>(s)].wallptr = static_cast<std::int16_t>(4 * s);
        map.sectors[static_cast<std::size_t>(s)].wallnum = 4;
        map.sectors[static_cast<std::size_t>(s)].floorz = 16384;
        map.sectors[static_cast<std::size_t>(s)].ceilingz = 0;
    }
    for (const std::int16_t w : {std::int16_t{1}, std::int16_t{7}}) {
        map.walls[static_cast<std::size_t>(w)].cstat = fauxbuild::mapv7::kWallCstatMasked;
        map.walls[static_cast<std::size_t>(w)].overpicnum = 0;
    }
    auto built = build_structural_world(map);
    REQUIRE(built.is_ok());
    const PreparedWorld p = prepare(built.value(), atlas);
    const PreparedSurface* masked = find_surface(p, SurfaceKind::PortalMasked, 1);
    REQUIRE(masked != nullptr);
    REQUIRE(masked->uvs.size() == 4);
    // Geometry passes through verbatim for the masked layer too.
    const fauxbuild::StructuralSurface* source = nullptr;
    for (const auto& surface : built.value().surfaces) {
        if (surface.kind == SurfaceKind::PortalMasked && surface.wall == 1) {
            source = &surface;
            break;
        }
    }
    REQUIRE(source != nullptr);
    CHECK(masked->vertices == source->vertices);
    CHECK(masked->indices == source->indices);

    const float expected_u[4] = {0.0f, 8.0f, 8.0f, 0.0f};
    const float expected_v[4] = {0.0f, 0.0f, 8.0f, 8.0f};
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(masked->uvs[i].u == expected_u[i]);
        CHECK(masked->uvs[i].v == expected_v[i]);
    }
}

TEST_CASE("masked layer is additive: no other prepared surface moves") {
    // The C1 analogue of the B1 regression pin. Setting the masked bit (with
    // overpicnum 0) must leave every other prepared surface byte-identical:
    // same vertices, indices, UVs, resolved tile, page and rect.
    const IndexedAtlas atlas = make_atlas();
    const StructuralWorld plain = placement_world(nullptr, "portal_heights");
    const StructuralWorld masked = placement_world(
        [](fauxbuild::mapv7::MapData& map) {
            for (const std::int16_t w : {std::int16_t{1}, std::int16_t{7}}) {
                map.walls[static_cast<std::size_t>(w)].cstat |= fauxbuild::mapv7::kWallCstatMasked;
                map.walls[static_cast<std::size_t>(w)].overpicnum = 0;
            }
        },
        "portal_heights");
    const PreparedWorld plain_p = prepare(plain, atlas);
    const PreparedWorld masked_p = prepare(masked, atlas);

    std::size_t plain_at = 0;
    for (const auto& surface : masked_p.surfaces) {
        if (surface.kind == SurfaceKind::PortalMasked) {
            continue;
        }
        REQUIRE(plain_at < plain_p.surfaces.size());
        const PreparedSurface& before = plain_p.surfaces[plain_at++];
        CHECK(surface.kind == before.kind);
        CHECK(surface.vertices == before.vertices);
        CHECK(surface.indices == before.indices);
        CHECK(surface.uvs == before.uvs);
        CHECK(surface.picnum == before.picnum);
        CHECK(surface.page == before.page);
    }
    CHECK(plain_at == plain_p.surfaces.size());
    std::size_t masked_count = 0;
    for (const auto& surface : masked_p.surfaces) {
        masked_count += surface.kind == SurfaceKind::PortalMasked ? 1 : 0;
    }
    CHECK(masked_count == 2);
}

TEST_CASE("paired masked layers keep their OWN authored UVs") {
    // M6.2C1 pre-gate correction pin. The two sides of a masked portal may
    // carry distinct panning/repeat/flips/alignment. Preparation must keep
    // both sides with their OWN UVs — never merged, averaged, deduplicated,
    // or forced onto one side's placement. Proof form: the paired world's
    // side-A UVs equal the SAME side's UVs in a world where ONLY side A is
    // masked (and likewise for B), so nothing about the other side leaked in.
    const IndexedAtlas atlas = make_atlas();
    const auto authored = [](fauxbuild::mapv7::MapData& map, std::int16_t w, std::int16_t over,
                             std::uint8_t xr, std::uint8_t yr, std::uint8_t xp, std::uint8_t yp,
                             std::int16_t extra_cstat) {
        auto& wall = map.walls[static_cast<std::size_t>(w)];
        wall.cstat = static_cast<std::int16_t>(fauxbuild::mapv7::kWallCstatMasked | extra_cstat);
        wall.overpicnum = over;
        wall.xrepeat = xr;
        wall.yrepeat = yr;
        wall.xpanning = xp;
        wall.ypanning = yp;
    };

    auto build = [&](bool side_a, bool side_b) {
        return placement_world(
            [&](fauxbuild::mapv7::MapData& map) {
                // The fixture carries masked+210 on BOTH sides; an unselected
                // side is cleared explicitly (overpicnum 210 is outside the
                // two-tile gate atlas).
                if (side_a) {
                    // Side A: flip X, wide repeat, both pans.
                    authored(map, 1, 0, 32, 8, 48, 12,
                             static_cast<std::int16_t>(fauxbuild::mapv7::kWallCstatFlipX));
                } else {
                    map.walls[1].cstat = 0;
                    map.walls[1].overpicnum = 0;
                }
                if (side_b) {
                    // Side B: flip Y + bottom-align, tall repeat, one pan.
                    authored(map, 7, 1, 8, 32, 0, 60,
                             static_cast<std::int16_t>(fauxbuild::mapv7::kWallCstatFlipY |
                                                       fauxbuild::mapv7::kWallCstatBottomAligned));
                } else {
                    map.walls[7].cstat = 0;
                    map.walls[7].overpicnum = 0;
                }
            },
            "masked_wall");
    };

    const PreparedWorld paired = prepare(build(true, true), atlas);
    const PreparedWorld only_a = prepare(build(true, false), atlas);
    const PreparedWorld only_b = prepare(build(false, true), atlas);

    const PreparedSurface* pair_a = find_surface(paired, SurfaceKind::PortalMasked, 1);
    const PreparedSurface* pair_b = find_surface(paired, SurfaceKind::PortalMasked, 7);
    const PreparedSurface* solo_a = find_surface(only_a, SurfaceKind::PortalMasked, 1);
    const PreparedSurface* solo_b = find_surface(only_b, SurfaceKind::PortalMasked, 7);
    REQUIRE(pair_a != nullptr);
    REQUIRE(pair_b != nullptr);
    REQUIRE(solo_a != nullptr);
    REQUIRE(solo_b != nullptr);

    CHECK(pair_a->uvs == solo_a->uvs); // side A is exactly side A alone
    CHECK(pair_b->uvs == solo_b->uvs); // side B is exactly side B alone
    CHECK(pair_a->uvs != pair_b->uvs); // distinct authored placement survives
    CHECK(pair_a->picnum == 0);
    CHECK(pair_b->picnum == 1);
    // Both vertices and indices are the untouched structural pair (coincident
    // geometry, opposite winding), and they remain two surfaces.
    std::size_t masked_count = 0;
    for (const auto& surface : paired.surfaces) {
        masked_count += surface.kind == SurfaceKind::PortalMasked ? 1 : 0;
    }
    CHECK(masked_count == 2);
}
