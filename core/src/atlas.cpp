#include "fauxbuild/atlas.hpp"

#include <algorithm>
#include <numeric>

namespace fauxbuild {

std::vector<std::uint8_t> IndexedAtlas::tile_bytes(std::uint32_t picnum) const {
    std::vector<std::uint8_t> out;
    if (picnum >= tiles.size()) {
        return out;
    }
    const auto& tile = tiles[picnum];
    if (!tile.populated || tile.page < 0 || tile.page >= page_count) {
        return out;
    }
    out.reserve(static_cast<std::size_t>(tile.width) * tile.height);
    const std::size_t page_base = static_cast<std::size_t>(tile.page) * page_width * page_height;
    for (std::int32_t row = 0; row < tile.height; ++row) {
        const std::size_t row_base =
            page_base + static_cast<std::size_t>(tile.y + row) * page_width + tile.x;
        out.insert(out.end(), pixels.begin() + static_cast<std::ptrdiff_t>(row_base),
                   pixels.begin() + static_cast<std::ptrdiff_t>(row_base + tile.width));
    }
    return out;
}

namespace {

struct ShelfPacker {
    std::int32_t page_w = 0;
    std::int32_t page_h = 0;
    std::int32_t page = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t row_h = 0;

    struct Placement {
        std::int32_t page, x, y;
    };

    // Classic shelf/row allocation: tiles are placed left to right in
    // ascending picnum order; a tile that does not fit the current row
    // starts a new row, and one that does not fit the page starts a new
    // page. No sorting, no reflow: placement depends only on the tile
    // sequence and the page dimensions.
    bool place(std::int32_t w, std::int32_t h, Placement& out) {
        if (w > page_w || h > page_h) {
            return false;
        }
        if (x + w > page_w) {
            x = 0;
            y += row_h;
            row_h = 0;
        }
        if (y + h > page_h) {
            ++page;
            x = 0;
            y = 0;
            row_h = 0;
        }
        out = Placement{page, x, y};
        x += w;
        row_h = std::max(row_h, h);
        return true;
    }
};

struct RangeClaim {
    std::int32_t start = 0;
    std::int32_t end = 0;
    std::size_t index = 0; // index into arts
};

} // namespace

Result<IndexedAtlas> build_indexed_atlas(const std::vector<ArtData>& arts,
                                         const AtlasOptions& options) {
    if (options.page_width <= 0 || options.page_height <= 0) {
        return Result<IndexedAtlas>::err({"atlas", 0, "atlas.options", ErrorCode::InvalidCount,
                                          "page dimensions must be positive"});
    }
    if (arts.empty()) {
        return Result<IndexedAtlas>::err(
            {"atlas", 0, "atlas.sources", ErrorCode::NotFound, "no ART files to compose"});
    }

    // Pass 1: validate every claim, collect ranges, compute the namespace.
    std::vector<RangeClaim> claims;
    claims.reserve(arts.size());
    for (std::size_t i = 0; i < arts.size(); ++i) {
        const auto& art = arts[i];
        const std::string record = "art.range[" + std::to_string(i) + "]";
        if (art.localtilestart < 0 || art.localtileend < art.localtilestart) {
            return Result<IndexedAtlas>::err({"atlas", 0, record, ErrorCode::InvalidRange,
                                              "malformed range " +
                                                  std::to_string(art.localtilestart) + ".." +
                                                  std::to_string(art.localtileend)});
        }
        const auto declared = static_cast<std::uint64_t>(art.localtileend) -
                              static_cast<std::uint64_t>(art.localtilestart) + 1;
        if (declared != art.tiles.size()) {
            return Result<IndexedAtlas>::err({"atlas", 0, record, ErrorCode::InvalidRange,
                                              "range implies " + std::to_string(declared) +
                                                  " tiles but data holds " +
                                                  std::to_string(art.tiles.size())});
        }
        // numtiles is not consulted at all. The published ART description says
        // it is unused and that the tile namespace comes from localtilestart/
        // localtileend; real content shows only that it is not an *upper*
        // bound (2816 declared in all 13 shipped files while ranges reach
        // 3327). Nothing observed makes it a lower bound either, so treating
        // it as a floor was an invented semantic (review, PR #4). It stays
        // preserved raw in ArtData::numtiles_field and controls nothing.
        claims.push_back(RangeClaim{art.localtilestart, art.localtileend, i});
    }

    // Overlap detection: sort by (start, end) and require each claim to
    // begin after the previous one ends. Filenames carry no ordering; the
    // declared ranges are the only authority.
    std::sort(claims.begin(), claims.end(), [](const RangeClaim& a, const RangeClaim& b) {
        return std::make_pair(a.start, a.end) < std::make_pair(b.start, b.end);
    });
    for (std::size_t i = 1; i < claims.size(); ++i) {
        if (claims[i].start <= claims[i - 1].end) {
            return Result<IndexedAtlas>::err(
                {"atlas", 0, "atlas.range_overlap", ErrorCode::InvalidRange,
                 "picnum " + std::to_string(claims[i].start) + " claimed by ranges " +
                     std::to_string(claims[i - 1].start) + ".." +
                     std::to_string(claims[i - 1].end) + " and " + std::to_string(claims[i].start) +
                     ".." + std::to_string(claims[i].end)});
        }
    }

    // The namespace is exactly what the declared ranges claim.
    std::int64_t namespace_size = 0;
    for (const auto& art : arts) {
        namespace_size =
            std::max<std::int64_t>(namespace_size, static_cast<std::int64_t>(art.localtileend) + 1);
    }
    if (namespace_size > static_cast<std::int64_t>(0x7FFFFFFF)) {
        return Result<IndexedAtlas>::err(
            {"atlas", 0, "atlas.namespace", ErrorCode::TooLarge,
             "picnum namespace " + std::to_string(namespace_size) + " overflows int32"});
    }
    // Allocation guard BEFORE the entry/side tables materialize. This is a
    // resource limit over a *real* surface, not a mask for a wrong reading of
    // the format (D0011 precedent): the picnum namespace is legitimately
    // sparse, so two 24-byte ART files declaring ranges 0..0 and
    // 2000000000..2000000000 are individually valid yet size a 2e9-entry
    // namespace. Measured at 16 GiB resident before this cap existed.
    if (namespace_size > static_cast<std::int64_t>(options.max_tile_count)) {
        return Result<IndexedAtlas>::err(
            {"atlas", 0, "atlas.namespace", ErrorCode::TooLarge,
             "picnum namespace " + std::to_string(namespace_size) + " exceeds the cap " +
                 std::to_string(options.max_tile_count) + " (AtlasOptions::max_tile_count)"});
    }

    // Pass 2: build the entry table plus a picnum -> owning ART index side
    // table used by the texel copy pass.
    IndexedAtlas atlas;
    atlas.page_width = options.page_width;
    atlas.page_height = options.page_height;
    atlas.tile_count = static_cast<std::uint32_t>(namespace_size);
    atlas.tiles.resize(namespace_size);
    std::vector<std::size_t> art_of_picnum(namespace_size, static_cast<std::size_t>(-1));
    for (std::size_t i = 0; i < claims.size(); ++i) {
        const auto& art = arts[claims[i].index];
        atlas.art_ranges.emplace_back(claims[i].start, claims[i].end);
        for (std::size_t local = 0; local < art.tiles.size(); ++local) {
            const auto picnum = static_cast<std::uint32_t>(claims[i].start + local);
            const auto& tile = art.tiles[local];
            auto& entry = atlas.tiles[picnum];
            entry.picnum = picnum;
            entry.width = tile.width;
            entry.height = tile.height;
            entry.x_center = tile.meta.x_center;
            entry.y_center = tile.meta.y_center;
            entry.meta = tile.meta;
            entry.source = art.source;
            art_of_picnum[picnum] = claims[i].index;
            const bool zero_dim = tile.width <= 0 || tile.height <= 0;
            if (zero_dim) {
                continue; // explicit empty entry: page stays -1
            }
            const auto area =
                static_cast<std::uint64_t>(tile.width) * static_cast<std::uint64_t>(tile.height);
            if (area != tile.pixels.size()) {
                return Result<IndexedAtlas>::err(
                    {"atlas", 0, "atlas.payload[" + std::to_string(picnum) + "]",
                     ErrorCode::InvalidRange,
                     "pixel payload is " + std::to_string(tile.pixels.size()) +
                         " bytes but dimensions imply " + std::to_string(area)});
            }
            if (area > options.max_tile_area) {
                return Result<IndexedAtlas>::err(
                    {"atlas", 0, "atlas.tile_area[" + std::to_string(picnum) + "]",
                     ErrorCode::TooLarge,
                     "tile area " + std::to_string(area) + " exceeds cap " +
                         std::to_string(options.max_tile_area)});
            }
            entry.populated = true;
        }
    }

    // Pass 3: shelf-pack populated tiles in picnum order.
    ShelfPacker packer{options.page_width, options.page_height, 0, 0, 0, 0};
    for (std::uint32_t picnum = 0; picnum < atlas.tiles.size(); ++picnum) {
        if (!atlas.tiles[picnum].populated) {
            continue;
        }
        auto& entry = atlas.tiles[picnum];
        ShelfPacker::Placement placement{};
        if (!packer.place(entry.width, entry.height, placement)) {
            return Result<IndexedAtlas>::err(
                {"atlas", 0, "atlas.placement[" + std::to_string(picnum) + "]", ErrorCode::TooLarge,
                 "tile " + std::to_string(entry.width) + "x" + std::to_string(entry.height) +
                     " does not fit a page (" + std::to_string(options.page_width) + "x" +
                     std::to_string(options.page_height) + ")"});
        }
        entry.page = placement.page;
        entry.x = placement.x;
        entry.y = placement.y;
    }
    atlas.page_count = packer.page + 1;

    // Pass 4: allocate pages and copy texels. File order is column-major
    // per the published description (see atlas.hpp): pixels[x*h + y] lands
    // at page row y, column x. Padding stays index 0.
    const std::size_t page_bytes =
        static_cast<std::size_t>(options.page_width) * options.page_height;
    atlas.pixels.assign(page_bytes * atlas.page_count, 0);
    for (const auto& entry : atlas.tiles) {
        if (!entry.populated) {
            continue;
        }
        const auto& art = arts[art_of_picnum[entry.picnum]];
        const auto& tile = art.tiles[entry.picnum - art.localtilestart];
        const std::size_t page_base = static_cast<std::size_t>(entry.page) * page_bytes;
        for (std::int32_t x = 0; x < entry.width; ++x) {
            for (std::int32_t y = 0; y < entry.height; ++y) {
                atlas
                    .pixels[page_base + static_cast<std::size_t>(entry.y + y) * options.page_width +
                            (entry.x + x)] =
                    tile.pixels[static_cast<std::size_t>(x) * entry.height + y];
            }
        }
    }

    // Stats. An unpopulated picnum is a zero-dimension tile when some ART
    // record claims it, and a namespace gap when none does.
    for (std::uint32_t p = 0; p < atlas.tiles.size(); ++p) {
        const auto& entry = atlas.tiles[p];
        if (entry.populated) {
            ++atlas.populated_tiles;
        } else if (art_of_picnum[p] != static_cast<std::size_t>(-1)) {
            ++atlas.empty_zero_dim_tiles;
        } else {
            ++atlas.empty_gap_tiles;
        }
    }
    return Result<IndexedAtlas>::ok(std::move(atlas));
}

} // namespace fauxbuild
