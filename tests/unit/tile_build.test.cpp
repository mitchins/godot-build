#include <cstring>
#include <doctest/doctest.h>

#include "fauxbuild/art.hpp"
#include "fauxbuild/map_io.hpp"
#include "fauxbuild/palette.hpp"
#include "fauxbuild/tile_build.hpp"
#include "fauxbuild/tile_manifest.hpp"

using fauxbuild::build_art_from_tileset;
using fauxbuild::build_lookup_dat;
using fauxbuild::build_palette_dat;
using fauxbuild::BuiltArt;
using fauxbuild::ErrorCode;
using fauxbuild::parse_palette_spec;
using fauxbuild::parse_tile_manifest;
using fauxbuild::parse_tileset;
using fauxbuild::TileManifest;
using fauxbuild::TilesetDef;
using fauxbuild::write_tile_manifest;

namespace {

const char* kTilesetA = R"TS(tileset stability_a
tile alpha 8 8 pattern=solid color=1
tile beta  16 8 pattern=checker a=1 b=2 square=4
)TS";

const char* kTilesetB = R"TS(tileset stability_b
tile alpha 8 8 pattern=solid color=1
tile beta  16 8 pattern=checker a=1 b=2 square=4
tile gamma 32 32 pattern=ramp from=0 to=15
)TS";

} // namespace

TEST_CASE("manifest round-trips and validates") {
    TileManifest manifest;
    REQUIRE(manifest.assign("alpha", 8, 8, 0, 0, 0, 0, 0, 0x1111111111111111ull).is_ok());
    REQUIRE(manifest.assign("beta", 16, 8, 4, -4, 2, 4, 2, 0x2222222222222222ull).is_ok());
    auto text = write_tile_manifest(manifest);
    REQUIRE(text.is_ok());

    auto parsed = parse_tile_manifest(text.value(), "roundtrip");
    REQUIRE(parsed.is_ok());
    REQUIRE(parsed.value().entries.size() == 2);
    CHECK(parsed.value().entries[0].picnum == 0);
    CHECK(parsed.value().entries[1].picnum == 1);
    CHECK(parsed.value().entries[1].anim_type == 2);
    CHECK(parsed.value().entries[1].frames == 4);
    CHECK(parsed.value().entries[1].x_center == 4);
    CHECK(parsed.value().entries[1].y_center == -4);

    auto again = write_tile_manifest(parsed.value());
    REQUIRE(again.is_ok());
    CHECK(again.value() == text.value()); // canonical text
}

TEST_CASE("manifest rejects gaps, duplicates, and malformed lines") {
    SUBCASE("gap in picnum sequence") {
        auto parsed = parse_tile_manifest(
            "# fauxbuild tile manifest v2\n# h\n0 a 1 1 0 0 none 0 0 0000000000000000\n"
            "2 b 1 1 0 0 none 0 0 0000000000000000\n",
            "gap");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::InvalidCount);
    }
    SUBCASE("duplicate names") {
        auto parsed = parse_tile_manifest(
            "# fauxbuild tile manifest v2\n# h\n0 a 1 1 0 0 none 0 0 0000000000000000\n"
            "1 a 1 1 0 0 none 0 0 0000000000000000\n",
            "dup");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::InvalidName);
    }
    SUBCASE("bad field") {
        auto parsed = parse_tile_manifest(
            "# fauxbuild tile manifest v2\n# h\n0 a x 1 0 0 none 0 0 0000000000000000\n", "bad");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::InvalidCount);
    }
    SUBCASE("wrong field count") {
        auto parsed = parse_tile_manifest(
            "# fauxbuild tile manifest v2\n# h\n0 a 1 1 0 0 none 0 0\n", "short");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().detail.find("expected 10 fields") != std::string::npos);
    }
    SUBCASE("malformed content hash") {
        for (const char* bad_hash : {"xyz", "ABCDEF0123456789", "0x11111111111111", "111"}) {
            auto parsed = parse_tile_manifest(
                std::string("# fauxbuild tile manifest v2\n# h\n0 a 1 1 0 0 none 0 0 ") + bad_hash +
                    "\n",
                "hash");
            REQUIRE_FALSE(parsed.is_ok());
            CHECK(parsed.error().detail.find("16 lowercase hex digits") != std::string::npos);
        }
    }
}

TEST_CASE("assign is append-only: names are immutable once assigned") {
    TileManifest manifest;
    REQUIRE(manifest.assign("alpha", 8, 8, 0, 0, 0, 0, 0, 0x1111111111111111ull).is_ok());
    auto dup = manifest.assign("alpha", 8, 8, 0, 0, 0, 0, 0, 0x1111111111111111ull);
    REQUIRE_FALSE(dup.is_ok());
    CHECK(dup.error().code == ErrorCode::InvalidName);
}

TEST_CASE("STABILITY: adding a tile leaves every prior picnum unchanged") {
    // The property the M4 brief demands as a test, not a comment: after a new
    // tile is added to the source and everything is rebuilt, every previously
    // assigned picnum still means exactly the same tile (name, dims, pivot,
    // animation), and the new tile gets max+1.
    auto a = parse_tileset(kTilesetA, "a");
    REQUIRE(a.is_ok());
    TileManifest manifest;
    auto built_a = build_art_from_tileset(a.value(), manifest);
    REQUIRE(built_a.is_ok());
    const TileManifest manifest_a = built_a.value().manifest;
    REQUIRE(manifest_a.entries.size() == 2);

    auto b = parse_tileset(kTilesetB, "b");
    REQUIRE(b.is_ok());
    auto built_b = build_art_from_tileset(b.value(), manifest_a);
    REQUIRE(built_b.is_ok());
    const auto& manifest_b = built_b.value().manifest;

    REQUIRE(manifest_b.entries.size() == 3);
    for (const auto& prior : manifest_a.entries) {
        const auto* after = manifest_b.find(prior.name);
        REQUIRE(after != nullptr);
        CHECK(after->picnum == prior.picnum);
        CHECK(after->width == prior.width);
        CHECK(after->height == prior.height);
        CHECK(after->x_center == prior.x_center);
        CHECK(after->y_center == prior.y_center);
        CHECK(after->anim_type == prior.anim_type);
        CHECK(after->frames == prior.frames);
        CHECK(after->speed == prior.speed);
        // The ART content for a prior picnum is byte-identical too.
        CHECK(built_b.value().art.tiles[static_cast<std::size_t>(prior.picnum)].pixels ==
              built_a.value().art.tiles[static_cast<std::size_t>(prior.picnum)].pixels);
    }
    CHECK(manifest_b.find("gamma")->picnum == 2); // max+1
}

TEST_CASE("STABILITY: same name and shape but different pixels is rejected") {
    // Review finding (slice 3): name/dims/pivot/animation do not pin a picnum's
    // meaning. A tile that keeps all of them and changes only its pattern would
    // silently redefine what every map referencing that picnum draws.
    const char* before = "tileset t\ntile alpha 8 8 pattern=solid color=1\n";
    const char* after = "tileset t\ntile alpha 8 8 pattern=solid color=9\n";

    auto a = parse_tileset(before, "before");
    REQUIRE(a.is_ok());
    TileManifest manifest;
    auto built = build_art_from_tileset(a.value(), manifest);
    REQUIRE(built.is_ok());

    auto b = parse_tileset(after, "after");
    REQUIRE(b.is_ok());
    auto rebuilt = build_art_from_tileset(b.value(), built.value().manifest);
    REQUIRE_FALSE(rebuilt.is_ok());
    CHECK(rebuilt.error().record == "build.stability");
    CHECK(rebuilt.error().detail.find("different pixels") != std::string::npos);

    // The identical source must still rebuild cleanly — the check must not be
    // so strict that a no-op rebuild fails.
    auto same = parse_tileset(before, "same");
    REQUIRE(same.is_ok());
    auto again = build_art_from_tileset(same.value(), built.value().manifest);
    REQUIRE(again.is_ok());
    CHECK(again.value().manifest.entries.size() == built.value().manifest.entries.size());
}

TEST_CASE("STABILITY: an accepted repaint keeps the picnum and refreshes the hash") {
    // D0014 rule 4: maps own numbers, artists own art. Redrawing a tile while
    // keeping its picnum is ordinary authoring; only *unacknowledged* drift is
    // an error. The first contract shipped here forbade repaints outright,
    // which no artist could work under (human ruling, slice-3 review).
    const char* before = "tileset t\ntile alpha 8 8 pattern=solid color=1\n"
                         "tile beta 16 8 pattern=checker a=1 b=2 square=4\n";
    const char* repainted = "tileset t\ntile alpha 8 8 pattern=solid color=9\n"
                            "tile beta 16 8 pattern=checker a=1 b=2 square=4\n";
    auto a = parse_tileset(before, "before");
    REQUIRE(a.is_ok());
    TileManifest manifest;
    auto built = build_art_from_tileset(a.value(), manifest);
    REQUIRE(built.is_ok());
    const auto* original = built.value().manifest.find("alpha");
    REQUIRE(original != nullptr);
    const std::int32_t picnum_before = original->picnum;
    const std::uint64_t hash_before = original->content;

    auto b = parse_tileset(repainted, "repainted");
    REQUIRE(b.is_ok());

    SUBCASE("unacknowledged is still a hard error naming the remedy") {
        auto rebuilt = build_art_from_tileset(b.value(), built.value().manifest);
        REQUIRE_FALSE(rebuilt.is_ok());
        CHECK(rebuilt.error().detail.find("--accept-tile-update alpha") != std::string::npos);
    }
    SUBCASE("accepted keeps the number and updates the record") {
        fauxbuild::TileUpdateAcceptance accepted;
        accepted.accepted_names = {"alpha"};
        auto rebuilt = build_art_from_tileset(b.value(), built.value().manifest, accepted);
        REQUIRE(rebuilt.is_ok());
        const auto* after = rebuilt.value().manifest.find("alpha");
        REQUIRE(after != nullptr);
        CHECK(after->picnum == picnum_before); // the number never moves
        CHECK(after->content != hash_before);  // the record follows the art
        // Untouched tiles keep their hashes; acceptance is per-tile, not global.
        const auto* beta_before = built.value().manifest.find("beta");
        const auto* beta_after = rebuilt.value().manifest.find("beta");
        REQUIRE(beta_before != nullptr);
        REQUIRE(beta_after != nullptr);
        CHECK(beta_after->content == beta_before->content);
        CHECK(beta_after->picnum == beta_before->picnum);
    }
    SUBCASE("accepting one tile does not excuse another") {
        const char* both = "tileset t\ntile alpha 8 8 pattern=solid color=9\n"
                           "tile beta 16 8 pattern=checker a=3 b=4 square=4\n";
        auto c = parse_tileset(both, "both");
        REQUIRE(c.is_ok());
        fauxbuild::TileUpdateAcceptance accepted;
        accepted.accepted_names = {"alpha"};
        auto rebuilt = build_art_from_tileset(c.value(), built.value().manifest, accepted);
        REQUIRE_FALSE(rebuilt.is_ok());
        CHECK(rebuilt.error().detail.find("beta") != std::string::npos);
    }
}

TEST_CASE("build rejects malformed input that would index out of range") {
    auto ts = parse_tileset("tileset t\ntile a 8 8 pattern=solid color=1\n", "t");
    REQUIRE(ts.is_ok());
    SUBCASE("manifest starting at picnum 1") {
        TileManifest bad;
        bad.entries.push_back({1, "a", 8, 8, 0, 0, 0, 0, 0, 0});
        auto built = build_art_from_tileset(ts.value(), bad);
        CHECK_FALSE(built.is_ok());
    }
    SUBCASE("manifest with a gap") {
        TileManifest bad;
        bad.entries.push_back({0, "a", 8, 8, 0, 0, 0, 0, 0, 0});
        bad.entries.push_back({2, "b", 8, 8, 0, 0, 0, 0, 0, 0});
        auto built = build_art_from_tileset(ts.value(), bad);
        CHECK_FALSE(built.is_ok());
    }
    SUBCASE("empty tileset does not emit a negative ART range") {
        auto empty = parse_tileset("tileset empty\n", "empty");
        REQUIRE(empty.is_ok());
        TileManifest manifest;
        auto built = build_art_from_tileset(empty.value(), manifest);
        REQUIRE_FALSE(built.is_ok()); // previously emitted range 0..-1
    }
}

TEST_CASE("pattern DSL rejects unknown names and zero divisors") {
    // Both reached generation code: an unknown name fell through to a default
    // pixel, and square=0 / major=0 divided by zero (UBSan-confirmed).
    SUBCASE("unknown pattern") {
        auto ts = parse_tileset("tileset t\ntile a 8 8 pattern=nonesuch\n", "u");
        REQUIRE_FALSE(ts.is_ok());
        CHECK(ts.error().detail.find("unknown pattern") != std::string::npos);
    }
    SUBCASE("checker square=0") {
        auto ts = parse_tileset("tileset t\ntile a 8 8 pattern=checker a=1 b=2 square=0\n", "z");
        REQUIRE_FALSE(ts.is_ok());
    }
    SUBCASE("checker square negative") {
        auto ts = parse_tileset("tileset t\ntile a 8 8 pattern=checker a=1 b=2 square=-4\n", "n");
        REQUIRE_FALSE(ts.is_ok());
    }
    SUBCASE("grid major=0") {
        auto ts = parse_tileset("tileset t\ntile a 8 8 pattern=grid major=0 line=1 bg=2\n", "g");
        REQUIRE_FALSE(ts.is_ok());
    }
}

TEST_CASE("picanm raw stays in sync with the decoded fields") {
    // write_art serializes meta.raw. Mutating the decoded frames/anim_type
    // without repacking made the emitted ART disagree with the struct the unit
    // tests assert on — the exact "tests pass, artifact is wrong" failure.
    auto ts = parse_tileset(
        "tileset t\nanim spin 8 8 frames=4 type=forward speed=3 pattern=solid color=2\n", "p");
    REQUIRE(ts.is_ok());
    TileManifest manifest;
    auto built = build_art_from_tileset(ts.value(), manifest);
    REQUIRE(built.is_ok());
    for (const auto& tile : built.value().art.tiles) {
        CHECK((tile.meta.raw & 0x3F) == tile.meta.frames);
        CHECK(((tile.meta.raw >> 6) & 0x3) == tile.meta.anim_type);
    }
    // And the round-trip through the file agrees with the in-memory view.
    auto bytes = fauxbuild::write_art(built.value().art);
    REQUIRE(bytes.is_ok());
    auto reparsed = fauxbuild::read_art(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()),
        "p");
    REQUIRE(reparsed.is_ok());
    REQUIRE(reparsed.value().tiles.size() == built.value().art.tiles.size());
    for (std::size_t i = 0; i < reparsed.value().tiles.size(); ++i) {
        CHECK(reparsed.value().tiles[i].meta.frames == built.value().art.tiles[i].meta.frames);
        CHECK(reparsed.value().tiles[i].meta.anim_type ==
              built.value().art.tiles[i].meta.anim_type);
    }
}

TEST_CASE("manifest parser range-checks before narrowing") {
    // width 70000 narrowed to 4464 as an int16 and then passed validation.
    auto parsed = parse_tile_manifest(
        "# fauxbuild tile manifest v2\n# h\n0 a 70000 1 0 0 none 0 0 0000000000000000\n", "w");
    REQUIRE_FALSE(parsed.is_ok());
    CHECK(parsed.error().detail.find("out of range") != std::string::npos);
}

TEST_CASE("lookup writer rejects more swaps than the count field can hold") {
    fauxbuild::LookupData data;
    data.swaps.resize(256); // one byte can only carry 255
    auto written = fauxbuild::write_lookup_dat(data);
    REQUIRE_FALSE(written.is_ok());
}

TEST_CASE("STABILITY: reordering the source does not renumber picnums") {
    // Picnums follow the manifest, not source order.
    const char* one = "tileset t\ntile alpha 8 8 pattern=solid color=1\n"
                      "tile beta 16 8 pattern=checker a=1 b=2 square=4\n";
    const char* swapped = "tileset t\ntile beta 16 8 pattern=checker a=1 b=2 square=4\n"
                          "tile alpha 8 8 pattern=solid color=1\n";
    auto a = parse_tileset(one, "one");
    REQUIRE(a.is_ok());
    TileManifest manifest;
    auto built = build_art_from_tileset(a.value(), manifest);
    REQUIRE(built.is_ok());

    auto b = parse_tileset(swapped, "swapped");
    REQUIRE(b.is_ok());
    auto rebuilt = build_art_from_tileset(b.value(), built.value().manifest);
    REQUIRE(rebuilt.is_ok());
    for (const auto& prior : built.value().manifest.entries) {
        const auto* after = rebuilt.value().manifest.find(prior.name);
        REQUIRE(after != nullptr);
        CHECK(after->picnum == prior.picnum);
        CHECK(after->content == prior.content);
    }
}

TEST_CASE("STABILITY: removing or reshaping a tile is rejected") {
    auto b = parse_tileset(kTilesetB, "b");
    REQUIRE(b.is_ok());
    TileManifest manifest;
    auto built = build_art_from_tileset(b.value(), manifest);
    REQUIRE(built.is_ok());

    SUBCASE("removal") {
        auto a = parse_tileset(kTilesetA, "a"); // gamma missing
        REQUIRE(a.is_ok());
        auto rebuilt = build_art_from_tileset(a.value(), built.value().manifest);
        REQUIRE_FALSE(rebuilt.is_ok());
        CHECK(rebuilt.error().record == "build.stability");
    }
    SUBCASE("reshape") {
        TilesetDef mutated = b.value();
        mutated.tiles[0].width = 64;
        auto rebuilt = build_art_from_tileset(mutated, built.value().manifest);
        REQUIRE_FALSE(rebuilt.is_ok());
        CHECK(rebuilt.error().record == "build.stability");
    }
}

TEST_CASE("built ART parses, round-trips, and matches the manifest") {
    auto palette_spec = parse_palette_spec("palette p\nramp 0 256 0 0 0 -> 60 60 60\n", "p");
    REQUIRE(palette_spec.is_ok());

    const char* tileset = R"TS(tileset t
tile s 8 8 pattern=solid color=1
tile c 16 16 pattern=checker a=1 b=2 square=4
anim m 8 8 frames=4 type=forward speed=3 pattern=checker a=1 b=2 square=2
pivot m 4 4
)TS";
    auto parsed = parse_tileset(tileset, "t");
    REQUIRE(parsed.is_ok());
    TileManifest manifest;
    auto built = build_art_from_tileset(parsed.value(), manifest);
    REQUIRE(built.is_ok());

    REQUIRE(built.value().art.tiles.size() == 6); // 2 singles + 4 frames
    auto bytes = fauxbuild::write_art(built.value().art);
    REQUIRE(bytes.is_ok());
    const std::string_view v(reinterpret_cast<const char*>(bytes.value().data()),
                             bytes.value().size());
    auto reread = fauxbuild::read_art(v, "built");
    REQUIRE(reread.is_ok());
    CHECK(reread.value().tiles.size() == 6);

    // Animation span sits on the anchor: frames=4, type=forward, speed=3.
    const auto& anchor = reread.value().tiles[2].meta;
    CHECK(anchor.frames == 4);
    CHECK(anchor.anim_type == 2);
    CHECK(anchor.speed == 3);
    CHECK(anchor.x_center == 4);
    CHECK(anchor.y_center == 4);

    // Checker pattern is deterministic and phase-varying across frames.
    const auto& frame0 = reread.value().tiles[2].pixels;
    const auto& frame1 = reread.value().tiles[3].pixels;
    CHECK(frame0 != frame1);
}

TEST_CASE("palette build is deterministic, 6-bit, and shade-monotone") {
    const char* spec_text =
        "palette p\nramp 0 128 0 0 0 -> 60 60 60\nramp 128 128 60 0 0 -> 0 0 60\n"
        "swap 1 tint 60 0 0\nshades 32\n";
    auto spec = parse_palette_spec(spec_text, "p");
    REQUIRE(spec.is_ok());

    auto a = build_palette_dat(spec.value());
    auto b = build_palette_dat(spec.value());
    REQUIRE(a.is_ok());
    REQUIRE(b.is_ok());
    auto bytes_a = fauxbuild::write_palette_dat(a.value());
    auto bytes_b = fauxbuild::write_palette_dat(b.value());
    REQUIRE(bytes_a.is_ok());
    REQUIRE(bytes_b.is_ok());
    CHECK(bytes_a.value() == bytes_b.value()); // deterministic

    unsigned max_component = 0;
    for (const auto c : a.value().rgb) {
        max_component = std::max(max_component, static_cast<unsigned>(c));
    }
    CHECK(max_component <= 63);

    // Shade property: level 0 maps every entry to an identical-RGB entry
    // (duplicate colours legitimately collapse to their first occurrence);
    // darkest level maps everything to the globally darkest entry; mean
    // mapped luminance is non-increasing across levels.
    const auto& tables = a.value().shade_tables;
    std::size_t identity_rgb = 0;
    for (int i = 0; i < 256; ++i) {
        const std::uint8_t mapped = tables[static_cast<std::size_t>(i)];
        if (a.value().rgb[static_cast<std::size_t>(mapped * 3)] ==
                a.value().rgb[static_cast<std::size_t>(i * 3)] &&
            a.value().rgb[static_cast<std::size_t>(mapped * 3 + 1)] ==
                a.value().rgb[static_cast<std::size_t>(i * 3 + 1)] &&
            a.value().rgb[static_cast<std::size_t>(mapped * 3 + 2)] ==
                a.value().rgb[static_cast<std::size_t>(i * 3 + 2)]) {
            ++identity_rgb;
        }
    }
    CHECK(identity_rgb == 256); // every level-0 mapping preserves colour

    auto lum = [&](std::size_t index) {
        return a.value().rgb[index * 3] * 299 + a.value().rgb[index * 3 + 1] * 587 +
               a.value().rgb[index * 3 + 2] * 114;
    };
    double prev_mean = 1e30;
    for (int level = 0; level < 32; ++level) {
        double mean = 0;
        for (int i = 0; i < 256; ++i) {
            mean += static_cast<double>(lum(tables[static_cast<std::size_t>(level * 256 + i)]));
        }
        mean /= 256.0;
        CHECK(mean <= prev_mean + 1e-9); // monotone darkening
        prev_mean = mean;
    }

    auto lookup = build_lookup_dat(spec.value());
    REQUIRE(lookup.is_ok());
    REQUIRE(lookup.value().swaps.size() == 1);
    CHECK(lookup.value().swaps[0].index == 1);
}

TEST_CASE("tileset parser rejects bad DSL with line numbers") {
    SUBCASE("missing pattern") {
        auto parsed = parse_tileset("tileset t\ntile a 8 8\n", "bad");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().offset == 2); // line number
    }
    SUBCASE("bad anim frames") {
        auto parsed = parse_tileset("tileset t\nanim a 8 8 frames=1 type=forward "
                                    "pattern=solid color=1\n",
                                    "bad");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::InvalidCount);
    }
    SUBCASE("pivot without matching tile") {
        auto parsed = parse_tileset("tileset t\ntile a 8 8 pattern=solid color=1\n"
                                    "pivot z 0 0\n",
                                    "bad");
        REQUIRE_FALSE(parsed.is_ok());
    }
}

TEST_CASE("palette spec rejects undefined entries and out-of-range values") {
    SUBCASE("coverage gap") {
        auto parsed = parse_palette_spec("palette p\nentry 0 1 2 3\n", "gap");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::InvalidCount);
    }
    SUBCASE("7-bit component") {
        auto parsed = parse_palette_spec("palette p\nentry 0 64 0 0\n", "oor");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::OutOfBounds);
    }
    SUBCASE("swap index 0 reserved for the base palette") {
        auto parsed = parse_palette_spec("palette p\nswap 0 tint 1 1 1\n", "zero");
        REQUIRE_FALSE(parsed.is_ok());
        CHECK(parsed.error().code == ErrorCode::OutOfBounds);
    }
}

TEST_CASE("CRLF line endings parse identically in every text format") {
    // Windows checkouts (core.autocrlf) hand the parsers CRLF files; found
    // by the MSVC CI job (unknown directive '\r'). All three parsers strip
    // a trailing CR — this case pins the behavior.
    const char* tileset_lf = "tileset t\r\n\r\ntile a 8 8 pattern=solid color=1\r\n";
    auto ts = parse_tileset(tileset_lf, "crlf");
    REQUIRE(ts.is_ok());
    REQUIRE(ts.value().tiles.size() == 1);
    CHECK(ts.value().tiles[0].params[0] == 1); // value parsed past the CR

    TileManifest manifest;
    REQUIRE(manifest.assign("a", 8, 8, 0, 0, 0, 0, 0, 0x3333333333333333ull).is_ok());
    auto text = write_tile_manifest(manifest);
    REQUIRE(text.is_ok());
    auto crlf = parse_tile_manifest(std::string(text.value()).insert(1, 1, '\r'), "crlf-manifest");
    REQUIRE(crlf.is_ok());
    REQUIRE(crlf.value().entries.size() == 1);

    const char* palette_lf = "palette p\r\nentry 0 1 2 3\r\n";
    auto bad = parse_palette_spec(palette_lf, "crlf-palette");
    // coverage error expected (1 of 256 defined) — but NOT a directive error:
    REQUIRE_FALSE(bad.is_ok());
    CHECK(bad.error().record == "palette.coverage");
}
