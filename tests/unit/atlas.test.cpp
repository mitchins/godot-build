#include "fauxbuild/atlas.hpp"
#include "fauxbuild/asset_set.hpp"
#include "fauxbuild/grp_synth.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "fauxbuild/grp.hpp"
#include "fauxbuild/vfs.hpp"

using namespace fauxbuild;

namespace {

// Distinctive per-pixel index generators (asymmetric in x/y so a row/column
// transposition or off-by-one placement fails loudly, per the brief).
constexpr std::uint8_t index_a(std::int32_t x, std::int32_t y) {
    return static_cast<std::uint8_t>((17 + 29 * x + 53 * y) & 0xFF);
}
constexpr std::uint8_t index_b(std::int32_t x, std::int32_t y) {
    return static_cast<std::uint8_t>((0x80 + 3 * x + 5 * y) & 0xFF);
}
constexpr std::uint8_t index_c(std::int32_t x, std::int32_t y) {
    return static_cast<std::uint8_t>((0x40 + 7 * x + 11 * y) & 0xFF);
}
constexpr std::uint8_t index_d(std::int32_t x, std::int32_t y) {
    return static_cast<std::uint8_t>((0xC0 + x + y) & 0xFF);
}
constexpr std::uint8_t index_e(std::int32_t x, std::int32_t y) {
    return static_cast<std::uint8_t>((0x30 + 17 * x + y) & 0xFF);
}

std::uint32_t picanm_raw(std::uint32_t frames, std::uint32_t type, std::int32_t xc, std::int32_t yc,
                         std::uint32_t speed) {
    return (frames & 0x3F) | ((type & 0x3) << 6) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(xc)) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(yc)) << 16) |
           ((speed & 0xF) << 24);
}

ArtTile tile(std::int16_t w, std::int16_t h, std::uint32_t raw,
             std::uint8_t (*gen)(std::int32_t, std::int32_t)) {
    ArtTile t;
    t.width = w;
    t.height = h;
    t.meta.raw = raw;
    t.meta.frames = raw & 0x3F;
    t.meta.anim_type = (raw >> 6) & 0x3;
    t.meta.x_center = static_cast<std::int8_t>((raw >> 8) & 0xFF);
    t.meta.y_center = static_cast<std::int8_t>((raw >> 16) & 0xFF);
    t.meta.speed = (raw >> 24) & 0xF;
    t.pixels.resize(static_cast<std::size_t>(w) * h);
    std::size_t i = 0;
    for (std::int32_t x = 0; x < w; ++x) { // file order: column-major
        for (std::int32_t y = 0; y < h; ++y) {
            t.pixels[i++] = gen(x, y);
        }
    }
    return t;
}

// The brief's synthetic acceptance fixture:
//   ART A: range 0..3   (0 empty, 1 patterned 8x8, 2 non-square, 3 anim anchor)
//   ART B: range 8..10  (gap 4..7; 8/9 populated, 10 empty)
ArtData art_a() {
    ArtData art;
    art.version = 1;
    art.numtiles_field = 4; // single-file output declares its own extent
    art.localtilestart = 0;
    art.localtileend = 3;
    art.source = "mem:TILES000.ART";
    art.tiles.push_back(tile(0, 0, 0, nullptr));                          // empty
    art.tiles.push_back(tile(8, 8, picanm_raw(0, 0, 1, 2, 0), index_a));  // 8x8
    art.tiles.push_back(tile(5, 3, picanm_raw(0, 0, -1, 0, 0), index_b)); // non-square
    art.tiles.push_back(tile(4, 4, picanm_raw(3, 2, -2, 3, 5), index_c)); // anim anchor
    return art;
}

ArtData art_b() {
    ArtData art;
    art.version = 1;
    art.numtiles_field = 11; // declares the composed namespace extent
    art.localtilestart = 8;
    art.localtileend = 10;
    art.source = "mem:TILES001.ART";
    art.tiles.push_back(tile(6, 2, 0, index_d));
    art.tiles.push_back(tile(2, 6, 0, index_e));
    art.tiles.push_back(tile(0, 0, 0, nullptr)); // empty
    return art;
}

PaletteData minimal_palette(std::int16_t shades) {
    PaletteData p;
    for (std::size_t i = 0; i < kPaletteBytes; ++i) {
        p.rgb[i] = static_cast<std::uint8_t>((i * 7) & 0x3F);
    }
    p.num_shades = shades;
    p.shade_tables.resize(static_cast<std::size_t>(shades) * 256);
    for (std::int16_t r = 0; r < shades; ++r) {
        for (std::int32_t c = 0; c < 256; ++c) {
            p.shade_tables[static_cast<std::size_t>(r) * 256 + c] =
                static_cast<std::uint8_t>((c + 29 * r) & 0xFF);
        }
    }
    p.translucency.resize(kTranslucencyBytes);
    for (std::size_t i = 0; i < p.translucency.size(); ++i) {
        p.translucency[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    return p;
}

LookupData minimal_lookup() {
    LookupData l;
    LookupSwap s0;
    s0.index = 4;
    for (std::int32_t c = 0; c < 256; ++c) {
        s0.table[c] = static_cast<std::uint8_t>((c * 3) & 0xFF);
    }
    l.swaps.push_back(s0);
    l.alt_palettes.emplace_back();
    for (std::size_t i = 0; i < kPaletteBytes; ++i) {
        l.alt_palettes[0][i] = static_cast<std::uint8_t>((i * 5) & 0x3F);
    }
    return l;
}

std::vector<std::uint8_t> art_bytes(const ArtData& art) {
    auto out = write_art(art);
    REQUIRE(out.is_ok());
    return out.take();
}

std::vector<synth::GrpFileSpec> fixture_files() {
    auto palette = write_palette_dat(minimal_palette(8));
    REQUIRE(palette.is_ok());
    auto lookup = write_lookup_dat(minimal_lookup());
    REQUIRE(lookup.is_ok());
    return {
        {"TILES000.ART", art_bytes(art_a())},
        {"TILES001.ART", art_bytes(art_b())},
        {"PALETTE.DAT", palette.take()},
        {"LOOKUP.DAT", lookup.take()},
    };
}

} // namespace

TEST_CASE("multi-ART composition with explicit gaps") {
    auto atlas = build_indexed_atlas({art_a(), art_b()}, AtlasOptions{});
    REQUIRE(atlas.is_ok());
    const auto& a = atlas.value();
    CHECK(a.tile_count == 11); // namespace 0..10
    CHECK(a.art_ranges.size() == 2);
    CHECK(a.art_ranges[0].first == 0);
    CHECK(a.art_ranges[0].second == 3);
    CHECK(a.art_ranges[1].first == 8);
    CHECK(a.art_ranges[1].second == 10);
    CHECK(a.populated_tiles == 5);
    CHECK(a.empty_gap_tiles == 4);      // picnums 4..7
    CHECK(a.empty_zero_dim_tiles == 2); // picnums 0 and 10
    for (std::uint32_t p : {0u, 4u, 5u, 6u, 7u, 10u}) {
        CHECK_FALSE(a.tiles[p].populated);
        CHECK(a.tiles[p].page == -1); // explicit empty entry, no atlas space
    }
    for (std::uint32_t p : {1u, 2u, 3u, 8u, 9u}) {
        CHECK(a.tiles[p].populated);
    }
}

TEST_CASE("the namespace comes from declared ranges; numtiles controls nothing") {
    // D0015 rule 2, amended. The published description calls numtiles unused;
    // real content shows only that it is not an upper bound (2816 declared in
    // all 13 shipped files while ranges reach 3327). Nothing makes it a lower
    // bound, so an earlier "floor" reading was invented -- and it turned a
    // 24-byte file into a 12.8 GiB allocation.
    SUBCASE("a larger numtiles does not grow the namespace") {
        ArtData big = art_a(); // range 0..3
        big.numtiles_field = 20;
        auto atlas = build_indexed_atlas({big}, AtlasOptions{});
        REQUIRE(atlas.is_ok());
        CHECK(atlas.value().tile_count == 4); // end+1, not numtiles
        CHECK(atlas.value().empty_gap_tiles == 0);
    }
    SUBCASE("a smaller numtiles does not shrink or reject it") {
        ArtData beyond = art_b(); // range 8..10, the real-content shape
        beyond.numtiles_field = 9;
        auto grown = build_indexed_atlas({beyond}, AtlasOptions{});
        REQUIRE(grown.is_ok());
        CHECK(grown.value().tile_count == 11);
    }
    SUBCASE("numtiles is preserved verbatim even though it is ignored") {
        ArtData art = art_a();
        art.numtiles_field = 2816; // the real shipped value
        auto atlas = build_indexed_atlas({art}, AtlasOptions{});
        REQUIRE(atlas.is_ok());
        CHECK(atlas.value().tile_count == 4);
        CHECK(art.numtiles_field == 2816); // the field itself is untouched
    }
    SUBCASE("an absurd numtiles is simply irrelevant, not merely capped") {
        // The exact 24-byte DoS shape. Under the floor reading this sized a
        // 2e9-entry namespace; now it is one tile and no cap is involved.
        ArtData huge;
        huge.version = 1;
        huge.numtiles_field = 2000000000;
        huge.localtilestart = 0;
        huge.localtileend = 0;
        ArtTile zero;
        huge.tiles.push_back(zero);
        auto atlas = build_indexed_atlas({huge}, AtlasOptions{});
        REQUIRE(atlas.is_ok());
        CHECK(atlas.value().tile_count == 1);
    }
}

TEST_CASE("overlapping claimed ranges are rejected") {
    ArtData dup = art_a();  // 4 tiles
    dup.localtilestart = 3; // 3..6 overlaps art_a's 0..3
    dup.localtileend = 6;
    dup.numtiles_field = 7; // self-consistent: >= end+1
    dup.source = "mem:DUP.ART";
    auto atlas = build_indexed_atlas({art_a(), dup}, AtlasOptions{});
    REQUIRE_FALSE(atlas.is_ok());
    CHECK(atlas.error().record == std::string("atlas.range_overlap"));
    CHECK(atlas.error().code == ErrorCode::InvalidRange);
}

TEST_CASE("malformed ranges and inconsistent counts are rejected") {
    ArtData bad = art_a();
    bad.localtileend = bad.localtilestart - 1;
    auto atlas = build_indexed_atlas({bad}, AtlasOptions{});
    REQUIRE_FALSE(atlas.is_ok());
    CHECK(atlas.error().code == ErrorCode::InvalidRange);

    ArtData mismatch = art_a();
    mismatch.tiles.pop_back(); // range implies 4, data holds 3
    atlas = build_indexed_atlas({mismatch}, AtlasOptions{});
    REQUIRE_FALSE(atlas.is_ok());
    CHECK(atlas.error().code == ErrorCode::InvalidRange);
}

TEST_CASE("pixel payload outside declared dimensions is rejected") {
    ArtData leaky = art_a();
    leaky.tiles[1].pixels.push_back(0xFF); // w*h + 1 bytes
    auto atlas = build_indexed_atlas({leaky}, AtlasOptions{});
    REQUIRE_FALSE(atlas.is_ok());
    CHECK(atlas.error().record == std::string("atlas.payload[1]"));
}

TEST_CASE("atlas placement overflow is rejected") {
    AtlasOptions tiny;
    tiny.page_width = 4;
    tiny.page_height = 8;
    auto atlas = build_indexed_atlas({art_a()}, tiny); // tile 1 is 8x8
    REQUIRE_FALSE(atlas.is_ok());
    CHECK(atlas.error().record == std::string("atlas.placement[1]"));
    CHECK(atlas.error().code == ErrorCode::TooLarge);
}

TEST_CASE("namespace allocation is capped before any materialization") {
    // D0015 rule 3: the cap guards a *real* surface. The picnum namespace is
    // legitimately sparse, so two individually valid 24-byte ART files can
    // declare ranges two billion apart with no misinterpretation anywhere.
    // Measured at 16 GiB resident before this cap existed.
    ArtData low;
    low.version = 1;
    low.localtilestart = 0;
    low.localtileend = 0;
    low.tiles.push_back(ArtTile{});
    ArtData far;
    far.version = 1;
    far.localtilestart = 2000000000;
    far.localtileend = 2000000000;
    far.source = "mem:FAR.ART";
    far.tiles.push_back(ArtTile{});
    auto atlas = build_indexed_atlas({low, far}, AtlasOptions{});
    REQUIRE_FALSE(atlas.is_ok());
    CHECK(atlas.error().code == ErrorCode::TooLarge);
    CHECK(atlas.error().record == std::string("atlas.namespace"));

    // The cap is a dial, not a wall: a namespace within the cap passes
    // (demonstrated small — the point is the comparison, not gigabytes).
    AtlasOptions tight;
    tight.max_tile_count = 3;
    auto small_reject = build_indexed_atlas({art_a()}, tight); // namespace 4
    REQUIRE_FALSE(small_reject.is_ok());
    CHECK(small_reject.error().code == ErrorCode::TooLarge);
    tight.max_tile_count = 4;
    auto small_ok = build_indexed_atlas({art_a()}, tight);
    REQUIRE(small_ok.is_ok());
    CHECK(small_ok.value().tile_count == 4);
}

TEST_CASE("index bytes survive into the page exactly (no transposition)") {
    auto atlas = build_indexed_atlas({art_a(), art_b()}, AtlasOptions{});
    REQUIRE(atlas.is_ok());
    const auto& a = atlas.value();
    REQUIRE(a.page_count == 1);

    // The byte-count tripwire: authoritative storage is exactly one index
    // per texel — 2048*2048*1, never four times that.
    CHECK(a.pixels.size() == static_cast<std::size_t>(a.page_width) * a.page_height * a.page_count);

    // Tile 1 (8x8, index_a) must sit somewhere in page 0 with its bytes
    // row-major: page[(y0+py)*W + x0+px] == index_a(px,py).
    const auto& t1 = a.tiles[1];
    REQUIRE(t1.populated);
    REQUIRE(t1.page == 0);
    REQUIRE(t1.x + t1.width <= a.page_width);
    REQUIRE(t1.y + t1.height <= a.page_height);
    bool saw_asymmetry = false;
    for (std::int32_t px = 0; px < t1.width; ++px) {
        for (std::int32_t py = 0; py < t1.height; ++py) {
            const std::size_t at = static_cast<std::size_t>(t1.y + py) * a.page_width + (t1.x + px);
            CHECK(a.pixels[at] == index_a(px, py));
            saw_asymmetry = saw_asymmetry || index_a(px, py) != index_a(py, px);
        }
    }
    // The fixture must actually be transpose-detecting (test-of-the-test).
    REQUIRE(saw_asymmetry);

    // Non-square tile 2 (5x3) and the high-bit tiles from ART B.
    const auto& t2 = a.tiles[2];
    for (std::int32_t px = 0; px < t2.width; ++px) {
        for (std::int32_t py = 0; py < t2.height; ++py) {
            CHECK(a.pixels[static_cast<std::size_t>(t2.y + py) * a.page_width + (t2.x + px)] ==
                  index_b(px, py));
        }
    }
    const auto& t8 = a.tiles[8];
    CHECK(a.pixels[static_cast<std::size_t>(t8.y) * a.page_width + t8.x] == index_d(0, 0));
    const auto& t9 = a.tiles[9];
    CHECK(a.pixels[static_cast<std::size_t>(t9.y + 5) * a.page_width + (t9.x + 1)] ==
          index_e(1, 5));

    // Convenience accessor agrees with the page bytes.
    const auto direct = a.tile_bytes(2);
    REQUIRE(direct.size() == 15);
    CHECK(direct[0] == index_b(0, 0));
    CHECK(direct[14] == index_b(4, 2));
}

TEST_CASE("picanm and pivot metadata are preserved on atlas entries") {
    auto atlas = build_indexed_atlas({art_a(), art_b()}, AtlasOptions{});
    REQUIRE(atlas.is_ok());
    const auto& t3 = atlas.value().tiles[3]; // animation anchor
    CHECK(t3.meta.frames == 3);
    CHECK(t3.meta.anim_type == 2);
    CHECK(t3.meta.speed == 5);
    CHECK(t3.x_center == -2);
    CHECK(t3.y_center == 3);
    CHECK(t3.meta.raw == picanm_raw(3, 2, -2, 3, 5)); // verbatim dword kept
    CHECK(t3.source == std::string("mem:TILES000.ART"));
    const auto& t1 = atlas.value().tiles[1];
    CHECK(t1.x_center == 1);
    CHECK(t1.y_center == 2);
}

TEST_CASE("atlas generation is deterministic across independent builds") {
    const auto files = fixture_files();
    REQUIRE(files.size() == 4);

    auto build = [&]() {
        std::vector<ArtData> arts;
        for (std::size_t i = 0; i < 2; ++i) {
            // Independent parse per build — no shared state.
            auto parsed =
                read_art(std::string_view(reinterpret_cast<const char*>(files[i].bytes.data()),
                                          files[i].bytes.size()),
                         files[i].name);
            REQUIRE(parsed.is_ok());
            arts.push_back(parsed.take());
        }
        auto atlas = build_indexed_atlas(arts, AtlasOptions{});
        REQUIRE(atlas.is_ok());
        return atlas.take();
    };

    const auto first = build();
    const auto second = build();
    CHECK(first.pixels == second.pixels);
    REQUIRE(first.tiles.size() == second.tiles.size());
    for (std::size_t i = 0; i < first.tiles.size(); ++i) {
        CHECK(first.tiles[i].page == second.tiles[i].page);
        CHECK(first.tiles[i].x == second.tiles[i].x);
        CHECK(first.tiles[i].y == second.tiles[i].y);
    }
    CHECK(first.page_count == second.page_count);
}

TEST_CASE("small pages wrap onto multiple pages without losing bytes") {
    // Hand-traced shelf layout for a 9x8 page, in picnum order:
    //   page 0: t1 8x8 @ (0,0)
    //   page 1: t2 5x3 @ (0,0), t3 4x4 @ (5,0), t8 6x2 @ (0,4)
    //   page 2: t9 2x6 @ (0,0)   (6+2 fits the row but 4+6 exceeds height)
    AtlasOptions small;
    small.page_width = 9;
    small.page_height = 8;
    auto atlas = build_indexed_atlas({art_a(), art_b()}, small);
    REQUIRE(atlas.is_ok());
    const auto& a = atlas.value();
    REQUIRE(a.page_count == 3);
    CHECK(a.pixels.size() == static_cast<std::size_t>(small.page_width) * small.page_height * 3);
    CHECK(a.tiles[1].page == 0);
    CHECK(a.tiles[2].page == 1);
    CHECK(a.tiles[3].page == 1);
    CHECK(a.tiles[3].x == 5); // shares page 1's first row with t2
    CHECK(a.tiles[8].page == 1);
    CHECK(a.tiles[9].page == 2);
    const auto& t1 = a.tiles[1];
    CHECK(a.pixels[static_cast<std::size_t>(t1.y + 7) * small.page_width + (t1.x + 7)] ==
          index_a(7, 7));
    const auto& t9 = a.tiles[9];
    const std::size_t page2_base =
        static_cast<std::size_t>(t9.page) * small.page_width * small.page_height;
    CHECK(
        a.pixels[page2_base + static_cast<std::size_t>(t9.y + 5) * small.page_width + (t9.x + 1)] ==
        index_e(1, 5));
}

TEST_CASE("AssetSet loads through a directory-style VFS and a GRP alike") {
    const auto files = fixture_files();

    // Loose-file route (development): MemoryMount stands in for a directory.
    Vfs loose;
    {
        auto mount = std::make_unique<MemoryMount>("loose");
        for (const auto& f : files) {
            mount->add_file(f.name, f.bytes);
        }
        loose.add_mount(std::move(mount));
    }
    auto set_loose = load_asset_set(loose);
    REQUIRE(set_loose.is_ok());
    CHECK(set_loose.value().arts.size() == 2);
    CHECK(set_loose.value().art_names[0] == std::string("TILES000.ART"));
    CHECK(set_loose.value().art_names[1] == std::string("TILES001.ART"));
    CHECK(set_loose.value().palette.num_shades == 8);
    CHECK(set_loose.value().lookup.swaps.size() == 1);
    CHECK(set_loose.value().lookup.alt_palettes.size() == 1);

    // Production route: mounted GRP, no extraction anywhere.
    const auto image = synth::build_grp(files);
    Vfs grp_vfs;
    auto mount = GrpMount::from_image("synthetic.grp", image);
    REQUIRE(mount.is_ok());
    grp_vfs.add_mount(mount.take());
    auto set_grp = load_asset_set(grp_vfs);
    REQUIRE(set_grp.is_ok());
    CHECK(set_grp.value().arts.size() == 2);
    CHECK(set_grp.value().palette.num_shades == 8);

    // The composed atlas is identical through either mount.
    auto atlas_loose = build_indexed_atlas(set_loose.value().arts, AtlasOptions{});
    auto atlas_grp = build_indexed_atlas(set_grp.value().arts, AtlasOptions{});
    REQUIRE(atlas_loose.is_ok());
    REQUIRE(atlas_grp.is_ok());
    CHECK(atlas_loose.value().pixels == atlas_grp.value().pixels);

    // Fail-closed discovery.
    Vfs empty_vfs;
    auto none = load_asset_set(empty_vfs);
    REQUIRE_FALSE(none.is_ok());
    CHECK(none.error().record == std::string("asset_set.arts"));

    Vfs no_palette;
    {
        auto m = std::make_unique<MemoryMount>("nopalette");
        m->add_file("TILES000.ART", files[0].bytes);
        m->add_file("LOOKUP.DAT", files[3].bytes);
        no_palette.add_mount(std::move(m));
    }
    auto missing = load_asset_set(no_palette);
    REQUIRE_FALSE(missing.is_ok());
    CHECK(missing.error().record == std::string("asset_set.palette"));
}

TEST_CASE("build_grp images round-trip through the parser") {
    const auto files = fixture_files();
    const auto image = synth::build_grp(files);
    const std::string_view view(reinterpret_cast<const char*>(image.data()), image.size());
    auto parsed = grp::parse(view, "roundtrip.grp");
    REQUIRE(parsed.is_ok());
    REQUIRE(parsed.value().entries.size() == files.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        CHECK(parsed.value().entries[i].name == files[i].name);
        CHECK(parsed.value().entries[i].size == files[i].bytes.size());
    }
}

TEST_CASE("is_art_name matches TILES*.ART and nothing else") {
    CHECK(is_art_name("TILES000.ART"));
    CHECK(is_art_name("TILES12.ART"));
    CHECK_FALSE(is_art_name("TILES.ART")); // no number
    CHECK_FALSE(is_art_name("TILES000.DAT"));
    CHECK_FALSE(is_art_name("PALETTE.DAT"));
    CHECK_FALSE(is_art_name("XTILES000.ART"));
}
