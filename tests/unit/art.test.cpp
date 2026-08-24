#include <cstring>
#include <doctest/doctest.h>

#include "fauxbuild/art.hpp"
#include "fauxbuild/map_io.hpp"

using fauxbuild::ArtData;
using fauxbuild::ArtTile;
using fauxbuild::ErrorCode;
using fauxbuild::PicanmBits;
using fauxbuild::read_art;
using fauxbuild::write_art;

namespace {

std::string_view view(const std::vector<std::uint8_t>& bytes) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void put32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(raw >> (8 * i)));
    }
}

void put16(std::vector<std::uint8_t>& out, std::int16_t value) {
    const auto raw = static_cast<std::uint16_t>(value);
    out.push_back(static_cast<std::uint8_t>(raw));
    out.push_back(static_cast<std::uint8_t>(raw >> 8));
}

struct Spec {
    std::int16_t w, h;
    std::uint32_t picanm;
};

// Hand-built ART image: exact control over every byte.
std::vector<std::uint8_t> make_art(std::int32_t start, std::int32_t end,
                                   const std::vector<Spec>& specs, std::int32_t numtiles,
                                   std::size_t pixel_cut = 0) {
    std::vector<std::uint8_t> out;
    put32(out, 1);
    put32(out, numtiles);
    put32(out, start);
    put32(out, end);
    std::uint64_t area = 0;
    for (const auto& spec : specs) {
        put16(out, spec.w);
        area += static_cast<std::uint64_t>(spec.w) * spec.h;
    }
    for (const auto& spec : specs) {
        put16(out, spec.h);
    }
    for (const auto& spec : specs) {
        put32(out, static_cast<std::int32_t>(spec.picanm));
    }
    std::uint64_t emitted = 0;
    for (const auto& spec : specs) {
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(spec.w) * spec.h; ++i) {
            if (pixel_cut == 0 || emitted < area - pixel_cut) {
                out.push_back(static_cast<std::uint8_t>(emitted & 0xFF));
            }
            ++emitted;
        }
    }
    return out;
}

} // namespace

TEST_CASE("handmade ART parses with exact picanm decode and byte-identical round-trip") {
    const std::vector<Spec> specs = {
        {4, 3, 0x09C70525u},
        {2, 2, 0x00000000u},
        {0, 5, 0u},          // zero-width tile: no pixels, legal
        {3, 1, 0x0900C305u}, // xc=0xC3(-61), yc=0, speed=9, type=0
    };
    auto bytes = make_art(10, 13, specs, 2816);
    auto parsed = read_art(view(bytes), "handmade");
    REQUIRE(parsed.is_ok());
    REQUIRE(parsed.value().tiles.size() == 4);
    CHECK(parsed.value().localtilestart == 10);
    CHECK(parsed.value().localtileend == 13);
    CHECK(parsed.value().numtiles_field == 2816);

    // picanm decode: spec 0 = 0x09C70525
    // frames = 0x25 & 0x3F = 0x25(37); type = (0x25>>6)&3 = 0; xc = 0x05 = 5;
    // yc = 0xC7 = -57; speed = (0x09C70525>>24)&0xF = 0x0... top byte 0x09 -> 9
    const auto& first = parsed.value().tiles[0];
    CHECK(first.meta.frames == (0x09C70525u & 0x3F));
    CHECK(first.meta.anim_type == 0);
    CHECK(first.meta.x_center == 5);
    CHECK(first.meta.y_center == -57);
    CHECK(first.meta.speed == 9);
    CHECK(first.meta.raw == 0x09C70525u);
    CHECK(first.pixels.size() == 12);

    CHECK(parsed.value().tiles[2].pixels.empty()); // zero-dim tile

    auto written = write_art(parsed.value());
    REQUIRE(written.is_ok());
    CHECK(written.value() == bytes);
}

TEST_CASE("picanm bit layout decodes every field per the published description") {
    struct Case {
        std::uint32_t raw;
        std::uint8_t frames, type, speed;
        std::int8_t xc, yc;
    };
    const Case cases[] = {
        {0x00000000u, 0, 0, 0, 0, 0},
        {0x0000003Fu, 63, 0, 0, 0, 0},
        {0x000000C0u, 0, 3, 0, 0, 0},
        {0x0000FF00u, 0, 0, 0, -1, 0}, // xc = 0xFF signed; bits 7-6 are clear
        {0x00800000u, 0, 0, 0, 0, -128},
        {0x0F000000u, 0, 0, 15, 0, 0},
        {0x0FC7FFFFu, 63, 3, 15, -1, -57}, // frames 63 + type 3 fills low byte 0xFF
    };
    for (const auto& c : cases) {
        auto bytes = make_art(0, 0, {{1, 1, c.raw}}, 1);
        auto parsed = read_art(view(bytes), "bits");
        REQUIRE(parsed.is_ok());
        const auto& meta = parsed.value().tiles[0].meta;
        CHECK(meta.frames == c.frames);
        CHECK(meta.anim_type == c.type);
        CHECK(meta.speed == c.speed);
        CHECK(meta.x_center == c.xc);
        CHECK(meta.y_center == c.yc);
    }
}

TEST_CASE("malformed ART fails closed with named errors") {
    SUBCASE("unsupported version") {
        auto bytes = make_art(0, 0, {{1, 1, 0}}, 1);
        bytes[0] = 2;
        auto parsed = read_art(view(bytes), "v2");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::UnsupportedVersion);
    }
    SUBCASE("descending tile range") {
        auto bytes = make_art(5, 4, {}, 1);
        auto parsed = read_art(view(bytes), "rev");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::InvalidCount);
    }
    SUBCASE("dims region exceeds the file") {
        auto bytes = make_art(0, 0, {{1, 1, 0}}, 1);
        auto parsed = read_art(view(bytes).substr(0, 20), "cut");
        REQUIRE_FALSE(parsed.is_ok());
    }
    SUBCASE("pixel area longer than the file") {
        auto bytes = make_art(0, 1, {{4, 4, 0}, {4, 4, 0}}, 2, 8);
        auto parsed = read_art(view(bytes), "shortpixels");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::Truncated);
    }
    SUBCASE("trailing bytes rejected") {
        auto bytes = make_art(0, 0, {{2, 2, 0}}, 1);
        bytes.push_back(0xEE);
        auto parsed = read_art(view(bytes), "extra");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::TrailingData);
    }
}

TEST_CASE("every truncation of a handmade ART fails safely") {
    const auto bytes = make_art(0, 2, {{3, 3, 0x100}, {2, 4, 0}, {5, 1, 0}}, 3);
    for (std::size_t len = 0; len < bytes.size(); ++len) {
        const std::string_view partial(view(bytes).substr(0, len));
        auto parsed = read_art(partial, "trunc");
        if (parsed.is_ok()) {
            FAIL("unexpected parse success at length ", len);
        } else {
            CHECK(parsed.error().offset <= len);
        }
    }
    CHECK(read_art(view(bytes), "full").is_ok());
}

TEST_CASE("writer validates structure before emitting") {
    SUBCASE("range/count mismatch") {
        ArtData data;
        data.localtilestart = 0;
        data.localtileend = 3; // implies 4 tiles
        data.tiles.resize(3);
        auto written = write_art(data);
        REQUIRE_FALSE(written.is_ok());
        CHECK(written.error().code == ErrorCode::InvalidCount);
    }
    SUBCASE("pixel/dimension mismatch") {
        ArtData data;
        data.localtilestart = 0;
        data.localtileend = 0;
        ArtTile tile;
        tile.width = 4;
        tile.height = 4;
        tile.pixels.resize(10); // not 16
        data.tiles.push_back(tile);
        auto written = write_art(data);
        REQUIRE_FALSE(written.is_ok());
        CHECK(written.error().code == ErrorCode::InvalidCount);
    }
}

TEST_CASE("real-file model: 256-tile layout closes exactly like shipped content") {
    // Mirrors the corroborated structure (no real bytes): contiguous 256-tile
    // range, mostly zero-dim/zero-anim tiles, small animated minority.
    std::vector<Spec> specs;
    specs.reserve(256);
    for (int i = 0; i < 256; ++i) {
        if (i % 64 == 7) {
            specs.push_back({8, 8, 0x02000043u}); // frames 3, type 1, speed 2
        } else if (i % 5 == 0) {
            specs.push_back({0, 0, 0}); // empty tile
        } else {
            specs.push_back(
                {static_cast<std::int16_t>(i % 17 + 1), static_cast<std::int16_t>(i % 13 + 1), 0});
        }
    }
    auto bytes = make_art(0, 255, specs, 2816);
    auto parsed = read_art(view(bytes), "model");
    REQUIRE(parsed.is_ok());
    REQUIRE(parsed.value().tiles.size() == 256);
    CHECK(parsed.value().numtiles_field == 2816);
    auto written = write_art(parsed.value());
    REQUIRE(written.is_ok());
    CHECK(written.value() == bytes);
}
