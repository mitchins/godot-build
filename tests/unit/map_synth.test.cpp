#include <doctest/doctest.h>

#include "fauxbuild/map_io.hpp"
#include "fauxbuild/map_synth.hpp"
#include "fauxbuild/map_validate.hpp"

using fauxbuild::read_map;
using fauxbuild::validate_map;
using fauxbuild::synth::serialize_map_fixture;

namespace {

std::string_view view(const std::vector<std::uint8_t>& bytes) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

TEST_CASE("fixture generation is deterministic") {
    for (const auto& name : fauxbuild::synth::map_fixture_names()) {
        const auto a = serialize_map_fixture(name);
        const auto b = serialize_map_fixture(name);
        REQUIRE(a.is_ok());
        REQUIRE(b.is_ok());
        CHECK(a.value() == b.value());
    }
}

TEST_CASE("fixture worlds have the intended shape") {
    SUBCASE("minimal") {
        auto world = fauxbuild::synth::map_fixture("minimal");
        REQUIRE(world.is_ok());
        CHECK(world.value().sectors.size() == 1);
        CHECK(world.value().walls.size() == 4);
        CHECK(world.value().sprites.empty());
    }
    SUBCASE("two_sector_portal") {
        auto world = fauxbuild::synth::map_fixture("two_sector_portal");
        REQUIRE(world.is_ok());
        REQUIRE(world.value().walls.size() == 8);
        CHECK(world.value().walls[1].nextwall == 7);
        CHECK(world.value().walls[7].nextwall == 1);
        CHECK(world.value().walls[1].nextsector == 1);
        CHECK(world.value().walls[7].nextsector == 0);
    }
    SUBCASE("portal_heights") {
        auto world = fauxbuild::synth::map_fixture("portal_heights");
        REQUIRE(world.is_ok());
        REQUIRE(world.value().walls.size() == 8);
        // Real-content vertical ordering: ceilingz < floorz numerically.
        // A spans [0, 16384]; B's window [4096, 8192] is strictly inside.
        CHECK(world.value().sectors[0].ceilingz == 0);
        CHECK(world.value().sectors[0].floorz == 16384);
        CHECK(world.value().sectors[1].ceilingz == 4096);
        CHECK(world.value().sectors[1].floorz == 8192);
    }
    SUBCASE("portal_step_floor") {
        auto world = fauxbuild::synth::map_fixture("portal_step_floor");
        REQUIRE(world.is_ok());
        CHECK(world.value().sectors[1].ceilingz == 0);
        CHECK(world.value().sectors[1].floorz == 8192);
    }
    SUBCASE("double_hole") {
        auto world = fauxbuild::synth::map_fixture("double_hole");
        REQUIRE(world.is_ok());
        CHECK(world.value().sectors.size() == 1);
        CHECK(world.value().sectors[0].wallnum == 12);
        CHECK(world.value().walls.size() == 12);
    }
    SUBCASE("multi_loop") {
        auto world = fauxbuild::synth::map_fixture("multi_loop");
        REQUIRE(world.is_ok());
        CHECK(world.value().sectors[0].wallnum == 8);
    }
    SUBCASE("slope_metadata") {
        auto world = fauxbuild::synth::map_fixture("slope_metadata");
        REQUIRE(world.is_ok());
        CHECK(world.value().sectors[0].floorheinum == 1000);
        CHECK(world.value().sectors[0].ceilingheinum == -1000);
        CHECK((world.value().sectors[0].floorstat & fauxbuild::mapv7::kStatSloped) != 0);
        CHECK((world.value().sectors[0].ceilingstat & fauxbuild::mapv7::kStatSloped) != 0);
    }
    SUBCASE("masked_wall") {
        auto world = fauxbuild::synth::map_fixture("masked_wall");
        REQUIRE(world.is_ok());
        CHECK((world.value().walls[1].cstat & fauxbuild::mapv7::kWallCstatMasked) != 0);
        CHECK(world.value().walls[1].overpicnum == 210);
    }
    SUBCASE("sprite_orientations") {
        auto world = fauxbuild::synth::map_fixture("sprite_orientations");
        REQUIRE(world.is_ok());
        REQUIRE(world.value().sprites.size() == 4);
        CHECK((world.value().sprites[1].cstat & fauxbuild::mapv7::kSpriteCstatAlignMask) ==
              fauxbuild::mapv7::kSpriteAlignWall);
        CHECK((world.value().sprites[2].cstat & fauxbuild::mapv7::kSpriteCstatAlignMask) ==
              fauxbuild::mapv7::kSpriteAlignFloor);
    }
    SUBCASE("max_reasonable_counts") {
        auto world = fauxbuild::synth::map_fixture("max_reasonable_counts");
        REQUIRE(world.is_ok());
        CHECK(world.value().sectors.size() == 64);
        CHECK(world.value().walls.size() == 256);
        CHECK(world.value().sprites.size() == 128);
        CHECK(validate_map(world.value()).ok());
    }
}

TEST_CASE("unknown fixture names are structured errors") {
    auto missing = serialize_map_fixture("nope");
    REQUIRE_FALSE(missing.is_ok());
    CHECK(missing.error().code == fauxbuild::ErrorCode::InvalidName);
}

TEST_CASE("fixture bytes match the observed MAP v7 section arithmetic") {
    auto bytes = serialize_map_fixture("square_room");
    REQUIRE(bytes.is_ok());
    const auto& b = bytes.value();
    auto parsed = read_map(view(b), "sizes");
    REQUIRE(parsed.is_ok());
    const auto& world = parsed.value();
    const std::size_t expected = 20 + 2 + world.sectors.size() * 40 + 2 + world.walls.size() * 32 +
                                 2 + world.sprites.size() * 44;
    CHECK(b.size() == expected);
}

TEST_CASE("fixtures carry the documented stat/cstat bits") {
    namespace v7 = fauxbuild::mapv7;

    SUBCASE("a sloped surface sets the slope bit, not just a heinum") {
        auto parsed = read_map(view(serialize_map_fixture("slope_metadata").value()), "slope");
        REQUIRE(parsed.is_ok());
        const auto& sector = parsed.value().sectors[0];
        CHECK(sector.floorheinum != 0);
        CHECK(sector.ceilingheinum != 0);
        CHECK((sector.floorstat & v7::kStatSloped) != 0);
        CHECK((sector.ceilingstat & v7::kStatSloped) != 0);
    }

    SUBCASE("a masked wall sets the masking bit and carries an overpicnum") {
        auto parsed = read_map(view(serialize_map_fixture("masked_wall").value()), "masked");
        REQUIRE(parsed.is_ok());
        std::size_t masked = 0;
        for (const auto& wall : parsed.value().walls) {
            if ((wall.cstat & v7::kWallCstatMasked) != 0) {
                ++masked;
                CHECK(wall.overpicnum != 0);
                CHECK(wall.nextwall != v7::kNoIndex); // masking only means something on a portal
            }
        }
        CHECK(masked == 2); // both sides of the shared edge
    }

    SUBCASE("sprite orientation is a two-bit field with one reserved value") {
        auto parsed =
            read_map(view(serialize_map_fixture("sprite_orientations").value()), "sprites");
        REQUIRE(parsed.is_ok());
        std::size_t face = 0, wall = 0, floor = 0;
        for (const auto& sprite : parsed.value().sprites) {
            const auto align = static_cast<std::int16_t>(sprite.cstat & v7::kSpriteCstatAlignMask);
            // 0x0030 is reserved; it appears in no legally owned map we have read.
            CHECK(align != v7::kSpriteCstatAlignMask);
            face += align == v7::kSpriteAlignFace;
            wall += align == v7::kSpriteAlignWall;
            floor += align == v7::kSpriteAlignFloor;
        }
        CHECK(wall == 1);
        CHECK(floor == 1);
        CHECK(face >= 1);
    }

    SUBCASE("no fixture sets the reserved orientation combination") {
        for (const auto& name : fauxbuild::synth::map_fixture_names()) {
            auto parsed = read_map(view(serialize_map_fixture(name).value()), name);
            REQUIRE(parsed.is_ok());
            for (const auto& sprite : parsed.value().sprites) {
                CHECK((sprite.cstat & v7::kSpriteCstatAlignMask) != v7::kSpriteCstatAlignMask);
            }
        }
    }
}
