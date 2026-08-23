#include <cstring>
#include <doctest/doctest.h>

#include "fauxbuild/map_diff.hpp"
#include "fauxbuild/map_io.hpp"
#include "fauxbuild/map_synth.hpp"
#include "fauxbuild/map_validate.hpp"

using fauxbuild::ErrorCode;
using fauxbuild::read_map;
using fauxbuild::validate_map;
using fauxbuild::write_map;
using fauxbuild::synth::map_fixture;
using fauxbuild::synth::serialize_map_fixture;

namespace {

std::string_view view(const std::vector<std::uint8_t>& bytes) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

const std::uint8_t* raw(const std::vector<std::uint8_t>& bytes) {
    return bytes.data();
}

// Deterministic byte-level header patcher for malformed-input tests.
struct Patch {
    std::size_t offset;
    std::uint8_t byte;
};

std::vector<std::uint8_t> patched(std::vector<std::uint8_t> bytes,
                                  const std::vector<Patch>& patches) {
    for (const auto& patch : patches) {
        REQUIRE(patch.offset < bytes.size());
        bytes[patch.offset] = patch.byte;
    }
    return bytes;
}

} // namespace

TEST_CASE("every fixture parses, validates, and round-trips byte-identically") {
    for (const auto& name : fauxbuild::synth::map_fixture_names()) {
        CAPTURE(name);
        auto bytes = serialize_map_fixture(name);
        REQUIRE(bytes.is_ok());

        auto parsed = read_map(view(bytes.value()), name);
        REQUIRE(parsed.is_ok());
        CHECK(parsed.value().source_hash ==
              fauxbuild::fnv1a64(raw(bytes.value()), bytes.value().size()));
        CHECK(validate_map(parsed.value()).ok());

        auto rewritten = write_map(parsed.value());
        REQUIRE(rewritten.is_ok());
        CHECK(rewritten.value() == bytes.value()); // canonical: byte-identical

        auto reparsed = read_map(view(rewritten.value()), name);
        REQUIRE(reparsed.is_ok());
        CHECK(fauxbuild::diff_maps(parsed.value(), reparsed.value()).identical);
    }
}

TEST_CASE("header decode is exact") {
    auto bytes = serialize_map_fixture("minimal");
    REQUIRE(bytes.is_ok());
    const auto& b = bytes.value();
    REQUIRE(b.size() >= 22);

    // version int32 == 7, then pose, then int16 sector count (observed layout).
    std::int32_t version = 0;
    std::memcpy(&version, b.data(), 4);
    CHECK(version == 7);

    auto parsed = read_map(view(b), "minimal");
    REQUIRE(parsed.is_ok());
    CHECK(parsed.value().start.x == 65536 / 2);
    CHECK(parsed.value().start.angle == 512);
    CHECK(parsed.value().start.sector == 0);
    CHECK(parsed.value().sectors.size() == 1);
    CHECK(parsed.value().walls.size() == 4);
    CHECK(parsed.value().sprites.empty());
}

TEST_CASE("unsupported version is rejected with the raw version in the error") {
    auto bytes = serialize_map_fixture("minimal");
    REQUIRE(bytes.is_ok());
    auto bad = patched(bytes.value(), {{0, 8}});
    auto parsed = read_map(view(bad), "bad-version");
    REQUIRE_FALSE(parsed.is_ok());
    CHECK(parsed.error().code == ErrorCode::UnsupportedVersion);
    CHECK(parsed.error().offset == 0);
    CHECK(parsed.error().record == "map.header");
}

TEST_CASE("every truncation of a valid map fails safely") {
    auto bytes = serialize_map_fixture("sprite_orientations");
    REQUIRE(bytes.is_ok());
    for (std::size_t len = 0; len < bytes.value().size(); ++len) {
        const std::string_view partial(view(bytes.value()).substr(0, len));
        auto parsed = read_map(partial, "truncated");
        if (parsed.is_ok()) {
            FAIL("unexpected parse success at length ", len);
        } else {
            CHECK(parsed.error().offset <= len);
        }
    }
    CHECK(read_map(view(bytes.value()), "full").is_ok());
}

TEST_CASE("profile limits fail closed with named errors") {
    auto bytes = serialize_map_fixture("minimal");
    REQUIRE(bytes.is_ok());
    // Section count offsets in the observed layout: numsectors at 20,
    // numwalls at 22 + 40*sectors, numsprites after the wall table.
    const std::size_t num_sectors_at = 20;
    const std::size_t num_walls_at = 22 + 40;
    const std::size_t num_sprites_at = num_walls_at + 2 + 4 * 32;

    SUBCASE("sectors") {
        auto over = patched(bytes.value(), {{num_sectors_at, 0xD0}, {num_sectors_at + 1, 0x07}});
        auto parsed = read_map(view(over), "over");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::TooManySectors);
    }
    SUBCASE("walls") {
        auto over = patched(bytes.value(), {{num_walls_at, 0x28}, {num_walls_at + 1, 0x23}});
        auto parsed = read_map(view(over), "over");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::TooManyWalls);
    }
    SUBCASE("sprites") {
        auto over = patched(bytes.value(), {{num_sprites_at, 0x88}, {num_sprites_at + 1, 0x13}});
        auto parsed = read_map(view(over), "over");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::TooManySprites);
    }
    SUBCASE("writer refuses over-limit worlds too") {
        auto world = map_fixture("minimal");
        REQUIRE(world.is_ok());
        // Copy the prototype first: assign(count, v) with v referring into the
        // same vector is use-after-free (caught by ASan in CI).
        const auto prototype = world.value().sectors[0];
        world.value().sectors.assign(fauxbuild::mapv7::kMaxSectors + 1, prototype);
        auto written = write_map(world.value());
        REQUIRE_FALSE(written.is_ok());
        CHECK(written.error().code == ErrorCode::TooManySectors);
    }
}

TEST_CASE("negative counts and trailing data are rejected") {
    auto bytes = serialize_map_fixture("minimal");
    REQUIRE(bytes.is_ok());

    // numsectors at offset 20 (int16) -> -1
    auto negative = patched(bytes.value(), {{20, 0xFF}, {21, 0xFF}});
    {
        auto parsed = read_map(view(negative), "negative");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::InvalidCount);
    }

    auto trailing = bytes.value();
    trailing.push_back(0xCC);
    auto parsed = read_map(view(trailing), "trailing");
    REQUIRE_FALSE(parsed.is_ok());
    CHECK(parsed.error().code == ErrorCode::TrailingData);
    CHECK(parsed.error().offset == trailing.size() - 1);
}

TEST_CASE("fnv1a64 matches the published FNV-1a 64-bit vectors") {
    // Pins the offset basis and prime so the name stays true and so
    // ci/check_corpus.py's Python port cannot silently diverge.
    auto h = [](const char* text) {
        return fauxbuild::fnv1a64(reinterpret_cast<const std::uint8_t*>(text), std::strlen(text));
    };
    CHECK(h("") == 0xcbf29ce484222325ull);
    CHECK(h("a") == 0xaf63dc4c8601ec8cull);
    CHECK(h("foobar") == 0x85944171f73967e8ull);
}

TEST_CASE("source hash distinguishes maps and survives canonical rewrite") {
    auto a = serialize_map_fixture("minimal");
    auto b = serialize_map_fixture("square_room");
    REQUIRE(a.is_ok());
    REQUIRE(b.is_ok());
    CHECK(a.value() != b.value());

    auto pa = read_map(view(a.value()), "a");
    auto pb = read_map(view(b.value()), "b");
    REQUIRE(pa.is_ok());
    REQUIRE(pb.is_ok());
    CHECK(pa.value().source_hash != pb.value().source_hash);
}
