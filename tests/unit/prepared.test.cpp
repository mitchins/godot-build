// D0020 prepared render world (M6 slice 2A). These gates prove the
// ARCHITECTURE — geometry passes through untouched, picnums resolve to real
// atlas tiles, UVs exist once per vertex and come from one authority. They do
// NOT assert historical Build constants: the UV conventions are provisional
// and the human visual gate on real content is what settles them.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "fauxbuild/atlas.hpp"
#include "fauxbuild/map_synth.hpp"
#include "fauxbuild/palette.hpp"
#include "fauxbuild/prepared.hpp"
#include "fauxbuild/structural.hpp"
#include "fauxbuild/tile_build.hpp"

using fauxbuild::build_indexed_atlas;
using fauxbuild::build_structural_world;
using fauxbuild::IndexedAtlas;
using fauxbuild::PaletteData;
using fauxbuild::prepare_world;
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
