#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "fauxbuild/art.hpp"
#include "fauxbuild/result.hpp"

namespace fauxbuild {

// Indexed atlas (M4 slice 4). The authoritative texel storage is ONE
// palette index per pixel; every RGBA form anywhere in the system is a
// derived, disposable preview built at a presentation boundary. The brief's
// tripwires: byte count == page_width*page_height*page_count (unit-tested),
// and check_layering pins this declaration and keeps Godot graphics types
// out of core/.
//
// Orientation: ART payloads are stored verbatim in file order, which the
// published description (PROVENANCE row 10) says is column-major; size
// arithmetic cannot verify that claim, so the ART model itself never
// interprets it. This atlas IS the presentation boundary where the claim is
// finally acted on: packing transposes file-order (column-major) bytes into
// row-major pages. The synthetic consumer-boundary fixtures pin the
// behavior; inspecting real GRP content in the preview is the human
// corroboration point.

struct AtlasTileEntry {
    std::uint32_t picnum = 0;
    bool populated = false; // false: gap picnum or zero-dimension tile
    std::int32_t page = -1; // -1 for unpopulated tiles (no atlas space)
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int16_t width = 0;
    std::int16_t height = 0;
    // Pivot and picanm metadata preserved from the owning ART record
    // (decoded fields plus the verbatim raw dword; M5 consumes this).
    std::int8_t x_center = 0;
    std::int8_t y_center = 0;
    PicanmBits meta{};
    std::string source; // provenance: origin of the owning ART file
};

struct IndexedAtlas {
    // Authoritative indexed texels: row-major pages, page_count slabs of
    // page_width*page_height bytes. Padding between tiles is index 0 and is
    // never referenced by any tile rect.
    std::vector<std::uint8_t> pixels;
    std::int32_t page_width = 0;
    std::int32_t page_height = 0;
    std::int32_t page_count = 0;
    std::uint32_t tile_count = 0;      // global picnum namespace size
    std::vector<AtlasTileEntry> tiles; // indexed by picnum

    std::uint32_t populated_tiles = 0;
    std::uint32_t empty_gap_tiles = 0;
    std::uint32_t empty_zero_dim_tiles = 0;
    // Declared ranges, one per contributing ART file, ascending by start.
    std::vector<std::pair<std::int32_t, std::int32_t>> art_ranges;

    // Row-major w*h bytes of one tile copied out of its page (convenience
    // accessor; the rect math over pixels[] is the load-bearing path).
    std::vector<std::uint8_t> tile_bytes(std::uint32_t picnum) const;
};

struct AtlasOptions {
    std::int32_t page_width = 2048;
    std::int32_t page_height = 2048;
    std::uint64_t max_tile_area = 1ull << 31; // int16 dims; 2^31 is generous
    // Namespace-size cap, enforced BEFORE any allocation: a 24-byte ART can
    // legitimately declare far-apart ranges (read_art is fine with that),
    // and without a cap the atlas would try to materialize a multi-GB entry
    // table from it (CodeRabbit, PR#4). Real content namespaces are ~3.3k;
    // 1M is three orders of headroom while staying fail-closed.
    std::uint32_t max_tile_count = 1u << 20;
};

// Compose ART files into one global picnum namespace and pack populated
// tiles into deterministic shelf-allocated indexed pages.
//
// Namespace policy (D0015): each file claims [localtilestart,
// localtileend]; the namespace size is max(localtileend + 1) over all
// contributing ART files. numtiles is preserved raw and consulted by nothing:
// the published description calls it unused, and real content shows only that
// it is not an upper bound — never that it is a lower one (D0015).// one global count (2816) while
// later files claim picnums beyond it (max observed end 3327), so it is never validated against a
// range. Gaps between and after claimed ranges become explicit empty picnums.
//
// Rejections: overlapping claimed ranges, malformed ranges, tile counts
// inconsistent with their own range, pixel payloads inconsistent with
// dimensions, tile areas over the cap, and tiles that cannot fit a page
// (atlas placement overflow).
//
// Determinism: placement is a pure function of (picnum order, dims,
// options) — same assets, same atlas byte for byte. Placement is a
// disposable product: picnum identity (the stable tile manifest, D0014) is
// the only permanent coordinate in the system.
Result<IndexedAtlas> build_indexed_atlas(const std::vector<ArtData>& arts,
                                         const AtlasOptions& options);

} // namespace fauxbuild
