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
    }
    SUBCASE("masked_wall") {
        auto world = fauxbuild::synth::map_fixture("masked_wall");
        REQUIRE(world.is_ok());
        CHECK((world.value().walls[1].cstat & 0x0002) != 0);
        CHECK(world.value().walls[1].overpicnum == 210);
    }
    SUBCASE("sprite_orientations") {
        auto world = fauxbuild::synth::map_fixture("sprite_orientations");
        REQUIRE(world.is_ok());
        REQUIRE(world.value().sprites.size() == 4);
        CHECK((world.value().sprites[1].cstat & 0x0008) != 0);
        CHECK((world.value().sprites[2].cstat & 0x0010) != 0);
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
