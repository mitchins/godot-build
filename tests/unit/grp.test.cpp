#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>

#include "fauxbuild/grp.hpp"
#include "fauxbuild/grp_synth.hpp"

using fauxbuild::ErrorCode;
using fauxbuild::grp::GrpDiagnostics;
using fauxbuild::grp::parse;
using fauxbuild::synth::generate_grp;

namespace {

std::string_view view(const std::vector<std::uint8_t>& bytes) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Builds a GRP image by hand so tests control every byte.
struct Handmade {
    std::vector<std::uint8_t> bytes;

    // 16-byte GRP header: 12-byte signature + uint32 file count. There is no
    // declared data-length field in the published format.
    Handmade& header(std::uint32_t count, const char* sig = "KenSilverman") {
        bytes.insert(bytes.end(), sig, sig + 12);
        u32(count);
        return *this;
    }
    Handmade& entry(const char* name, std::uint32_t size) {
        char field[12] = {};
        std::memcpy(field, name, std::strlen(name));
        bytes.insert(bytes.end(), field, field + 12);
        u32(size);
        return *this;
    }
    Handmade& u32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
        return *this;
    }
    Handmade& data(const char* payload) {
        bytes.insert(bytes.end(), payload, payload + std::strlen(payload));
        return *this;
    }
};

} // namespace

TEST_CASE("synthetic GRP enumerates and reads exact bytes") {
    const auto image = generate_grp({.seed = 7, .file_count = 12, .max_file_size = 300});
    GrpDiagnostics diags;
    auto parsed = parse(view(image), "synthetic", &diags);
    REQUIRE(parsed.is_ok());
    const auto& grp = parsed.value();

    CHECK(grp.file_count == 12);
    REQUIRE(grp.entries.size() == 12);
    CHECK(diags.warnings.empty());

    // Cross-check every entry against the generator's layout rules.
    std::uint64_t cursor = grp.data_start;
    for (std::uint32_t i = 0; i < grp.entries.size(); ++i) {
        const auto& entry = grp.entries[i];
        char expected[16];
        std::snprintf(expected, sizeof(expected), "SYN%04u.DAT", i);
        CHECK(entry.name == expected);
        CHECK(entry.offset == cursor);
        cursor += entry.size;
        CHECK(entry.offset + entry.size <= image.size());
    }
    CHECK(cursor == image.size());
}

TEST_CASE("handmade GRP parses with exact offsets and content slices") {
    Handmade made;
    made.header(2);
    made.entry("A.DAT", 3);
    made.entry("B.DAT", 4);
    made.data("xxx");
    made.data("yyyy");

    auto parsed = parse(view(made.bytes), "hand");
    REQUIRE(parsed.is_ok());
    const auto& grp = parsed.value();
    REQUIRE(grp.entries.size() == 2);
    CHECK(grp.entries[0].name == "A.DAT");
    CHECK(grp.entries[0].size == 3);
    CHECK(grp.entries[0].offset == 48); // 16-byte header + 2 * 16-byte directory
    CHECK(grp.entries[1].offset == 51);
    CHECK(std::memcmp(made.bytes.data() + 51, "yyyy", 4) == 0);
}

TEST_CASE("bad signature is rejected with the right record") {
    Handmade made;
    made.header(0, "NotSilverman");
    auto parsed = parse(view(made.bytes), "bad");
    REQUIRE_FALSE(parsed.is_ok());
    CHECK(parsed.error().code == ErrorCode::BadSignature);
    CHECK(parsed.error().offset == 0);
    CHECK(parsed.error().record == "grp.header");
}

TEST_CASE("directory larger than the container fails safely") {
    Handmade made;
    made.header(1000); // directory alone needs 16016 bytes; image is 16
    made.entry("A.DAT", 0);
    auto parsed = parse(view(made.bytes), "huge");
    REQUIRE_FALSE(parsed.is_ok());
    CHECK(parsed.error().code == ErrorCode::OutOfBounds);
    CHECK(parsed.error().record == "grp.directory");
}

TEST_CASE("entry data past the container end fails with entry context") {
    Handmade made;
    made.header(1);
    made.entry("A.DAT", 100);
    made.data("short"); // 5 of 100 bytes
    auto parsed = parse(view(made.bytes), "short");
    REQUIRE_FALSE(parsed.is_ok());
    CHECK(parsed.error().code == ErrorCode::OutOfBounds);
    CHECK(parsed.error().record == "grp.directory[0].data");
    CHECK(parsed.error().offset == 32); // where the file data starts
}

TEST_CASE("every truncation of a valid GRP fails safely") {
    const auto image = generate_grp({.seed = 3, .file_count = 6, .max_file_size = 64});
    for (std::size_t len = 0; len < image.size(); ++len) {
        std::string_view partial(view(image).substr(0, len));
        auto parsed = parse(partial, "truncated");
        if (parsed.is_ok()) {
            // A prefix can only parse if all declared data fits; with this
            // generator layout that is impossible before the final byte.
            FAIL("unexpected parse success at length ", len);
        } else {
            const auto& error = parsed.error();
            CHECK_FALSE(error.record.empty());
            CHECK(error.offset <= len);
        }
    }
    CHECK(parse(view(image), "full").is_ok());
}

TEST_CASE("illegal names are rejected, twelve-char names without NUL survive") {
    Handmade traversal;
    traversal.header(1);
    char bad[12] = {'a', '/', 'b', '.', '.', 'x'};
    traversal.bytes.insert(traversal.bytes.end(), bad, bad + 12);
    traversal.u32(0);
    auto rejected = parse(view(traversal.bytes), "traversal");
    REQUIRE_FALSE(rejected.is_ok());
    CHECK(rejected.error().code == ErrorCode::InvalidName);
    CHECK(rejected.error().record == "grp.directory[0]");

    Handmade full;
    full.header(1);
    char twelve[12]; // exactly 12 chars, no NUL in the field
    std::memcpy(twelve, "TWELVECHARSX", 12);
    full.bytes.insert(full.bytes.end(), twelve, twelve + 12);
    full.u32(0);
    auto accepted = parse(view(full.bytes), "twelve");
    REQUIRE(accepted.is_ok());
    CHECK(accepted.value().entries[0].name == "TWELVECHARSX");
}

TEST_CASE("duplicate names: first entry wins and a warning is recorded") {
    Handmade made;
    made.header(3);
    made.entry("DUP.DAT", 0);
    made.entry("dup.dat", 0);
    made.entry("OTHER.DAT", 0);
    GrpDiagnostics diags;
    auto parsed = parse(view(made.bytes), "dup", &diags);
    REQUIRE(parsed.is_ok());
    REQUIRE(parsed.value().entries.size() == 3);
    CHECK(parsed.value().entries[0].key == parsed.value().entries[1].key);
    REQUIRE(diags.warnings.size() == 1);
    CHECK(diags.warnings[0].find("DUP.DAT") != std::string::npos);
}

TEST_CASE("published 16-byte header: data begins right after the directory") {
    // Regression for the M2 review finding: the parser previously consumed a
    // phantom 4-byte declared-length field, shifting every directory entry and
    // rejecting real GRPs. Generator and parser both encoded the same mistake,
    // so only a hand-built spec-correct image catches it.
    Handmade made;
    made.header(2);
    made.entry("TILES000.ART", 3);
    made.entry("E1L1.MAP", 4);
    made.data("abc");
    made.data("wxyz");
    REQUIRE(made.bytes.size() == 16 + 2 * 16 + 3 + 4);

    GrpDiagnostics diags;
    auto parsed = parse(view(made.bytes), "spec", &diags);
    REQUIRE(parsed.is_ok());
    const auto& grp = parsed.value();
    CHECK(diags.warnings.empty());
    CHECK(grp.data_start == 48);
    REQUIRE(grp.entries.size() == 2);
    CHECK(grp.entries[0].name == "TILES000.ART"); // 12 chars, no NUL terminator
    CHECK(grp.entries[0].offset == 48);
    CHECK(grp.entries[1].name == "E1L1.MAP");
    CHECK(grp.entries[1].offset == 51);
    CHECK(grp.entries[1].size == 4);
}

TEST_CASE("empty and single-byte inputs fail safely") {
    auto empty = parse(std::string_view(""), "empty");
    REQUIRE_FALSE(empty.is_ok());
    CHECK(empty.error().code == ErrorCode::Truncated);

    auto one = parse(std::string_view("K", 1), "one");
    REQUIRE_FALSE(one.is_ok());
    CHECK(one.error().offset == 0);
}

TEST_CASE("file count above the parser limit is rejected before allocating") {
    // A clamped reserve only defers the allocation: entries still grow to 64
    // bytes each, so an unbounded count amplifies untrusted input ~4x into
    // process memory. kMaxEntryCount is the bound (D0011). The rejection must
    // happen on the header alone, without the directory being present.
    Handmade made;
    made.header(fauxbuild::grp::kMaxEntryCount + 1);
    auto parsed = parse(view(made.bytes), "oversized");
    REQUIRE_FALSE(parsed.is_ok());
    CHECK(parsed.error().code == fauxbuild::ErrorCode::TooLarge);
    CHECK(parsed.error().record == "grp.header");
    CHECK(parsed.error().offset == 12);
    // 16 bytes of header rejected it; no directory was read.
    CHECK(made.bytes.size() == 16);
}

TEST_CASE("file count exactly at the parser limit is accepted") {
    constexpr std::uint32_t kCount = fauxbuild::grp::kMaxEntryCount;
    Handmade made;
    made.header(kCount);
    for (std::uint32_t i = 0; i < kCount; ++i) {
        char name[12];
        std::snprintf(name, sizeof(name), "F%05u.DAT", i);
        made.entry(name, 0);
    }
    auto parsed = parse(view(made.bytes), "at-limit");
    REQUIRE(parsed.is_ok());
    CHECK(parsed.value().entries.size() == kCount);
}

TEST_CASE("directory larger than the reserve clamp parses to every entry") {
    // The parser reserves at most kEntryReserveClamp (4096) entries up front so a
    // declared count cannot amplify into a multi-gigabyte allocation before any
    // entry is validated. Growth past the clamp must stay transparent: this
    // directory declares 5000 zero-size entries in ~80 KiB of image.
    constexpr std::uint32_t kCount = 5000;
    Handmade made;
    made.header(kCount);
    for (std::uint32_t i = 0; i < kCount; ++i) {
        char name[12];
        std::snprintf(name, sizeof(name), "F%05u.DAT", i);
        made.entry(name, 0);
    }
    REQUIRE(made.bytes.size() == 16 + 16ull * kCount);

    auto parsed = parse(view(made.bytes), "clamped");
    REQUIRE(parsed.is_ok());
    const auto& grp = parsed.value();
    REQUIRE(grp.entries.size() == kCount);
    CHECK(grp.entries.front().name == "F00000.DAT");
    CHECK(grp.entries.back().name == "F04999.DAT");
    // Zero-size entries all sit at the first data byte.
    CHECK(grp.entries.front().offset == grp.data_start);
    CHECK(grp.entries.back().offset == grp.data_start);
}
