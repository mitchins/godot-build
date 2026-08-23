#include <cstring>
#include <doctest/doctest.h>

#include "fauxbuild/map_io.hpp"
#include "fauxbuild/palette.hpp"

using fauxbuild::ErrorCode;
using fauxbuild::LookupData;
using fauxbuild::LookupSwap;
using fauxbuild::PaletteData;
using fauxbuild::read_lookup_dat;
using fauxbuild::read_palette_dat;
using fauxbuild::write_lookup_dat;
using fauxbuild::write_palette_dat;

namespace {

std::string_view view(const std::vector<std::uint8_t>& bytes) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

// Canonical synthetic PALETTE.DAT: n shade tables, optional extra tables.
std::vector<std::uint8_t> make_palette(int shades, int extra_tables) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < 768; ++i) {
        out.push_back(static_cast<std::uint8_t>(i % 64)); // 6-bit components
    }
    put_u16(out, static_cast<std::uint16_t>(shades));
    for (int t = 0; t < shades + extra_tables; ++t) {
        for (int i = 0; i < 256; ++i) {
            out.push_back(static_cast<std::uint8_t>((t * 7 + i) & 0xFF));
        }
    }
    out.insert(out.end(), 65536, 0x5A);
    return out;
}

std::vector<std::uint8_t> make_lookup(int swaps, int alts) {
    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(swaps));
    for (int s = 0; s < swaps; ++s) {
        out.push_back(static_cast<std::uint8_t>(s + 1)); // index
        for (int i = 0; i < 256; ++i) {
            out.push_back(static_cast<std::uint8_t>((s * 13 + i) & 0xFF));
        }
    }
    for (int a = 0; a < alts; ++a) {
        for (int i = 0; i < 768; ++i) {
            out.push_back(static_cast<std::uint8_t>((a * 31 + i) % 64));
        }
    }
    return out;
}

} // namespace

TEST_CASE("synthetic PALETTE.DAT round-trips byte-identically") {
    for (auto [shades, extras] : {std::pair<int, int>{0, 0}, {1, 0}, {32, 0}, {32, 32}, {0, 8}}) {
        CAPTURE(shades);
        CAPTURE(extras);
        const auto bytes = make_palette(shades, extras);
        auto parsed = read_palette_dat(view(bytes), "synthetic");
        REQUIRE(parsed.is_ok());
        CHECK(parsed.value().num_shades == shades);
        CHECK(parsed.value().shade_tables.size() == static_cast<std::size_t>(shades) * 256);
        CHECK(parsed.value().extra_tables.size() == static_cast<std::size_t>(extras) * 256);
        CHECK(parsed.value().translucency.size() == 65536);

        auto written = write_palette_dat(parsed.value());
        REQUIRE(written.is_ok());
        CHECK(written.value() == bytes); // preservation: byte-identical
    }
}

TEST_CASE("palette arithmetic failures are named and fail closed") {
    SUBCASE("too small for fixed sections") {
        std::vector<std::uint8_t> bytes(100, 0);
        auto parsed = read_palette_dat(view(bytes), "tiny");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::Truncated); // before 770 bytes
    }
    SUBCASE("table region not a multiple of 256") {
        auto bytes = make_palette(2, 0);
        bytes.push_back(0x00);
        auto parsed = read_palette_dat(view(bytes), "ragged");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::TrailingData);
    }
    SUBCASE("declared shade count exceeds the region") {
        auto bytes = make_palette(4, 0);
        bytes[768] = 0x40; // patch declared count 4 -> 64
        bytes[769] = 0x00;
        auto parsed = read_palette_dat(view(bytes), "overdeclared");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::InvalidCount);
        CHECK(parsed.error().record == "palette.header");
    }
    SUBCASE("negative shade count") {
        auto bytes = make_palette(1, 0);
        bytes[768] = 0xFF;
        bytes[769] = 0xFF; // -1
        auto parsed = read_palette_dat(view(bytes), "negative");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::InvalidCount);
    }
}

TEST_CASE("every truncation of a synthetic palette fails safely or is a valid file") {
    // Unlike MAP, the palette table region is implied by total size, so a
    // %-256-aligned truncation is a genuinely valid smaller file. The
    // invariant is: every prefix either yields a structured error or parses
    // and re-serializes byte-identically.
    const auto bytes = make_palette(3, 2);
    int ok = 0;
    int err = 0;
    for (std::size_t len = 0; len < bytes.size(); ++len) {
        const std::string_view partial(view(bytes).substr(0, len));
        auto parsed = read_palette_dat(partial, "trunc");
        if (parsed.is_ok()) {
            auto written = write_palette_dat(parsed.value());
            REQUIRE(written.is_ok());
            CHECK(written.value().size() == len);
            ++ok;
        } else {
            CHECK_FALSE(parsed.error().record.empty());
            ++err;
        }
    }
    CHECK(ok > 0);
    CHECK(err > 0);
    CHECK(read_palette_dat(view(bytes), "full").is_ok());
}

TEST_CASE("synthetic LOOKUP.DAT round-trips and reports structure") {
    for (auto [swaps, alts] : {std::pair<int, int>{0, 0}, {1, 0}, {25, 5}, {0, 3}}) {
        CAPTURE(swaps);
        CAPTURE(alts);
        const auto bytes = make_lookup(swaps, alts);
        auto parsed = read_lookup_dat(view(bytes), "synthetic");
        REQUIRE(parsed.is_ok());
        REQUIRE(parsed.value().swaps.size() == static_cast<std::size_t>(swaps));
        CHECK(parsed.value().alt_palettes.size() == static_cast<std::size_t>(alts));
        if (swaps > 0) {
            CHECK(parsed.value().swaps[0].index == 1);
        }

        auto written = write_lookup_dat(parsed.value());
        REQUIRE(written.is_ok());
        CHECK(written.value() == bytes);
    }
}

TEST_CASE("lookup arithmetic failures are named") {
    SUBCASE("swaps exceed file size") {
        std::vector<std::uint8_t> bytes{10, 1, 2, 3}; // declares 10 swaps, has 3 bytes
        auto parsed = read_lookup_dat(view(bytes), "short");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::OutOfBounds);
        CHECK(parsed.error().record == "lookup.header");
    }
    SUBCASE("remainder not a multiple of 768") {
        auto bytes = make_lookup(2, 0);
        bytes.push_back(0); // 1 + 514 + 1 = 516, 516-1-514=1, not %768
        auto parsed = read_lookup_dat(view(bytes), "ragged");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::TrailingData);
    }
    SUBCASE("empty input") {
        auto parsed = read_lookup_dat(std::string_view(""), "empty");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::Truncated);
    }
}

TEST_CASE("writer validates shape before emitting") {
    PaletteData bad;
    bad.num_shades = 4;
    bad.shade_tables.resize(256); // 1 table, not 4
    bad.translucency.resize(65536);
    auto written = write_palette_dat(bad);
    REQUIRE_FALSE(written.is_ok());
    CHECK(written.error().code == ErrorCode::InvalidCount);
}

TEST_CASE("real-file model: declared 32 + 32 extras + translucency closes exactly") {
    // Mirrors the legally owned PALETTE.DAT observation without embedding any
    // real content: 768 + 2 + 64*256 + 65536, count field says 32.
    auto bytes = make_palette(32, 32);
    REQUIRE(bytes.size() == 768 + 2 + 64 * 256 + 65536);
    bytes[768] = 32;
    bytes[769] = 0;
    auto parsed = read_palette_dat(view(bytes), "model");
    REQUIRE(parsed.is_ok());
    CHECK(parsed.value().num_shades == 32);
    CHECK(parsed.value().extra_tables.size() == 32 * 256);
}
