#include <doctest/doctest.h>

#include "fauxbuild/map_diff.hpp"
#include "fauxbuild/map_io.hpp"
#include "fauxbuild/map_synth.hpp"
#include "fauxbuild/map_validate.hpp"

using fauxbuild::diff_maps;
using fauxbuild::ErrorCode;
using fauxbuild::read_map;
using fauxbuild::Severity;
using fauxbuild::validate_map;
using fauxbuild::write_map;
using fauxbuild::synth::make_sector;
using fauxbuild::synth::make_sprite;
using fauxbuild::synth::make_wall;
using fauxbuild::synth::map_fixture;

namespace {

// Rebuild the map through the canonical writer so byte-level fixtures and
// in-memory worlds share one path; validation runs on parsed data regardless.
fauxbuild::mapv7::MapData round_trip(const fauxbuild::mapv7::MapData& world) {
    auto bytes = write_map(world);
    REQUIRE(bytes.is_ok());
    const std::string_view v(reinterpret_cast<const char*>(bytes.value().data()),
                             bytes.value().size());
    auto parsed = read_map(v, "validate");
    REQUIRE(parsed.is_ok());
    return parsed.take();
}

bool has_error(const fauxbuild::ValidationReport& report, ErrorCode code) {
    for (const auto& issue : report.issues) {
        if (issue.severity == Severity::Error && issue.code == code) {
            return true;
        }
    }
    return false;
}

fauxbuild::mapv7::MapData portal_world() {
    auto world = map_fixture("two_sector_portal");
    REQUIRE(world.is_ok());
    return world.take();
}

} // namespace

TEST_CASE("all fixtures validate clean") {
    for (const auto& name : fauxbuild::synth::map_fixture_names()) {
        CAPTURE(name);
        auto world = map_fixture(name);
        REQUIRE(world.is_ok());
        const auto report = validate_map(round_trip(world.value()));
        CHECK(report.ok());
    }
}

TEST_CASE("start sector validation") {
    auto world = map_fixture("minimal");
    REQUIRE(world.is_ok());

    SUBCASE("out of range") {
        world.value().start.sector = 5;
        CHECK(has_error(validate_map(round_trip(world.value())), ErrorCode::InvalidStartSector));
    }
    SUBCASE("-1 allowed only for zero-sector maps") {
        world.value().start.sector = -1;
        CHECK(has_error(validate_map(round_trip(world.value())), ErrorCode::InvalidStartSector));
        world.value().sectors.clear();
        world.value().walls.clear();
        world.value().sprites.clear();
        const auto report = validate_map(round_trip(world.value()));
        CHECK_FALSE(has_error(report, ErrorCode::InvalidStartSector));
    }
}

TEST_CASE("sector wall range validation is overflow-safe and fail-closed") {
    auto world = map_fixture("minimal");
    REQUIRE(world.is_ok());

    SUBCASE("range exceeds wall count") {
        world.value().sectors[0].wallnum = 100; // only 4 walls exist
        CHECK(
            has_error(validate_map(round_trip(world.value())), ErrorCode::InvalidSectorWallRange));
    }
    SUBCASE("extreme int16 values do not overflow the check") {
        world.value().sectors[0].wallptr = 32767;
        world.value().sectors[0].wallnum = 32767;
        CHECK(
            has_error(validate_map(round_trip(world.value())), ErrorCode::InvalidSectorWallRange));
    }
    SUBCASE("overlapping sector ranges are rejected") {
        const auto extra = make_sector(0, 4); // same range as sector 0
        world.value().sectors.push_back(extra);
        CHECK(
            has_error(validate_map(round_trip(world.value())), ErrorCode::InvalidSectorWallRange));
    }
    SUBCASE("unowned walls are rejected") {
        world.value().walls.push_back(make_wall(0, 0, 0));
        world.value().sectors[0].wallnum = 4; // wall 4 owned by nobody
        CHECK(has_error(validate_map(round_trip(world.value())), ErrorCode::InvalidTopology));
    }
}

TEST_CASE("loop topology validation") {
    SUBCASE("point2 out of global range") {
        auto world = map_fixture("minimal");
        REQUIRE(world.is_ok());
        world.value().walls[1].point2 = 99;
        CHECK(has_error(validate_map(round_trip(world.value())), ErrorCode::InvalidPoint2));
    }
    SUBCASE("point2 escapes the sector range") {
        auto world = map_fixture("two_sector_portal");
        REQUIRE(world.is_ok());
        // Wall 5 belongs to sector 1; point it into sector 0's span.
        world.value().walls[5].point2 = 1;
        CHECK(has_error(validate_map(round_trip(world.value())), ErrorCode::InvalidTopology));
    }
    SUBCASE("non-closing loop cannot hang validation") {
        auto world = map_fixture("multi_loop");
        REQUIRE(world.is_ok());
        // Redirect the hole loop into itself without returning to its start:
        // wall 5 -> wall 4 already happens; make wall 4 self-reference so the
        // walk from 4 closes trivially but 5,6,7 form a dangling chain.
        world.value().walls[4].point2 = 4;
        const auto report = validate_map(round_trip(world.value()));
        CHECK_FALSE(report.ok());
        CHECK(has_error(report, ErrorCode::InvalidTopology));
    }
    SUBCASE("multiple loops per sector are accepted") {
        auto world = map_fixture("multi_loop");
        REQUIRE(world.is_ok());
        CHECK(validate_map(round_trip(world.value())).ok());
    }
}

TEST_CASE("portal validation") {
    SUBCASE("nextwall out of range") {
        auto world = portal_world();
        world.walls[1].nextwall = 500;
        CHECK(has_error(validate_map(round_trip(world)), ErrorCode::InvalidNextWall));
    }
    SUBCASE("non-reciprocal portal") {
        auto world = portal_world();
        world.walls[7].nextwall = 0; // points at wall 0, not wall 1
        world.walls[7].nextsector = 0;
        CHECK(has_error(validate_map(round_trip(world)), ErrorCode::InvalidNextWall));
    }
    SUBCASE("nextsector without nextwall") {
        auto world = portal_world();
        world.walls[0].nextsector = 1;
        CHECK(has_error(validate_map(round_trip(world)), ErrorCode::InvalidNextSector));
    }
    SUBCASE("nextsector mismatching the owning sector of nextwall") {
        auto world = portal_world();
        world.walls[1].nextsector = 0; // wall 7 is owned by sector 1
        CHECK(has_error(validate_map(round_trip(world)), ErrorCode::InvalidNextSector));
    }
    SUBCASE("wall is its own portal partner") {
        // Degenerate but self-consistent: the wall mirrors itself and its
        // nextsector matches its own owner, so every reciprocity rule passes.
        // Caught in review as a validate-clean case (CodeRabbit, PR #2).
        auto world = portal_world();
        world.walls[1].nextwall = 1;
        world.walls[1].nextsector = 0; // sector 0 owns wall 1
        world.walls[7].nextwall = fauxbuild::mapv7::kNoIndex;
        world.walls[7].nextsector = fauxbuild::mapv7::kNoIndex;
        const auto report = validate_map(round_trip(world));
        CHECK(has_error(report, ErrorCode::InvalidNextWall));
        CHECK_FALSE(report.ok());
    }
}

TEST_CASE("sprite validation") {
    SUBCASE("invalid sectnum") {
        auto world = map_fixture("square_room");
        REQUIRE(world.is_ok());
        world.value().sprites[0].sectnum = 9999;
        CHECK(has_error(validate_map(round_trip(world.value())), ErrorCode::InvalidSpriteSector));
    }
    SUBCASE("sentinel -1 is accepted") {
        auto world = map_fixture("square_room");
        REQUIRE(world.is_ok());
        world.value().sprites[0].sectnum = -1;
        CHECK(validate_map(round_trip(world.value())).ok());
    }
}

TEST_CASE("validation is bounded on hostile topology") {
    // A wall chain that cycles without returning to any loop start must be
    // detected via the step bound, not by hoping the walk terminates.
    auto world = map_fixture("minimal");
    REQUIRE(world.is_ok());
    world.value().walls[0].point2 = 1;
    world.value().walls[1].point2 = 0;
    world.value().walls[2].point2 = 3;
    world.value().walls[3].point2 = 2;
    // Walk from 0: 0 -> 1 -> 0 closes at start... so walk 2: 2 -> 3 -> 2.
    // Both close: this is two 2-wall loops sharing nothing — actually valid
    // topology. Mutate into a crossing chain instead:
    world.value().walls[0].point2 = 2;
    world.value().walls[2].point2 = 0;
    const auto report = validate_map(round_trip(world.value()));
    CHECK_FALSE(report.ok()); // walls 1,3 orphaned; 0<->2 cross-loop
    CHECK(has_error(report, ErrorCode::InvalidTopology));
}

TEST_CASE("semantic diff pinpoints exactly the mutated field") {
    auto a = map_fixture("slope_metadata");
    auto b = map_fixture("slope_metadata");
    REQUIRE(a.is_ok());
    REQUIRE(b.is_ok());
    b.value().sectors[0].floorheinum = -42;
    b.value().walls[2].picnum = 77;

    const auto diff = diff_maps(a.value(), b.value());
    REQUIRE_FALSE(diff.identical);
    REQUIRE(diff.notes.size() == 2);
    bool seen_heinum = false;
    bool seen_picnum = false;
    for (const auto& note : diff.notes) {
        if (note.find("sector[0].floorheinum") != std::string::npos &&
            note.find("-42") != std::string::npos) {
            seen_heinum = true;
        }
        if (note.find("wall[2].picnum") != std::string::npos) {
            seen_picnum = true;
        }
    }
    CHECK(seen_heinum);
    CHECK(seen_picnum);
}
