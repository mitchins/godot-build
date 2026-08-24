#include "tools/fbtool/atlas_commands.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "fauxbuild/asset_set.hpp"
#include "fauxbuild/atlas.hpp"
#include "fauxbuild/vfs.hpp"

namespace fauxbuild::tool {

namespace {

struct Args {
    std::string grp;
    std::string dir;
    long page_width = 2048;
    long page_height = 2048;
    bool usage_error = false;
};

bool parse_long(const char* text, long& out) {
    char* end = nullptr;
    const long raw = std::strtol(text, &end, 10);
    if (end == nullptr || *end != '\0' || raw <= 0 || raw > 16384) {
        return false;
    }
    out = raw;
    return true;
}

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--grp") {
            const char* v = value();
            if (v == nullptr) {
                args.usage_error = true;
            } else {
                args.grp = v;
            }
        } else if (arg == "--dir") {
            const char* v = value();
            if (v == nullptr) {
                args.usage_error = true;
            } else {
                args.dir = v;
            }
        } else if (arg == "--page-width") {
            const char* v = value();
            if (v == nullptr || !parse_long(v, args.page_width)) {
                args.usage_error = true;
            }
        } else if (arg == "--page-height") {
            const char* v = value();
            if (v == nullptr || !parse_long(v, args.page_height)) {
                args.usage_error = true;
            }
        } else {
            args.usage_error = true;
        }
    }
    if (args.grp.empty() == args.dir.empty()) { // exactly one source
        args.usage_error = true;
    }
    return args;
}

} // namespace

int inspect_atlas(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    if (args.usage_error) {
        std::fprintf(stderr, "fbtool: inspect-atlas: usage: inspect-atlas --grp FILE | --dir DIR "
                             "[--page-width N] [--page-height N]\n");
        return 2;
    }

    Vfs vfs;
    std::string source_desc;
    if (!args.grp.empty()) {
        auto mount = GrpMount::create(args.grp);
        if (!mount.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", mount.error().to_string().c_str());
            return 1;
        }
        source_desc = mount.value()->describe();
        vfs.add_mount(mount.take());
    } else {
        auto mount = DirectoryMount::create(args.dir);
        if (!mount.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", mount.error().to_string().c_str());
            return 1;
        }
        source_desc = mount.value()->describe();
        vfs.add_mount(mount.take());
    }

    auto set = load_asset_set(vfs);
    if (!set.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", set.error().to_string().c_str());
        return 1;
    }

    AtlasOptions options;
    options.page_width = static_cast<std::int32_t>(args.page_width);
    options.page_height = static_cast<std::int32_t>(args.page_height);
    auto atlas = build_indexed_atlas(set.value().arts, options);
    if (!atlas.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", atlas.error().to_string().c_str());
        return 1;
    }
    const auto& a = atlas.value();

    std::printf("source: %s\n", source_desc.c_str());
    std::printf("ART sources: %zu (", set.value().art_names.size());
    for (std::size_t i = 0; i < set.value().art_names.size(); ++i) {
        std::printf("%s%s", i ? ", " : "", set.value().art_names[i].c_str());
    }
    std::printf(")\n");
    std::printf("declared ranges:");
    for (const auto& range : a.art_ranges) {
        std::printf(" %d..%d", range.first, range.second);
    }
    std::printf("\n");
    std::printf("global range: 0..%u (%u picnums)\n", a.tile_count == 0 ? 0 : a.tile_count - 1,
                a.tile_count);
    std::printf("populated tiles: %u\n", a.populated_tiles);
    std::printf("empty tiles: %u (%u gap, %u zero-dimension)\n",
                a.empty_gap_tiles + a.empty_zero_dim_tiles, a.empty_gap_tiles,
                a.empty_zero_dim_tiles);
    std::printf("atlas pages: %d (%dx%d)\n", a.page_count, a.page_width, a.page_height);
    std::printf("indexed bytes: %zu (one index per texel)\n", a.pixels.size());
    std::printf("palette: loaded through VFS (%d shade tables declared, %zu alt palettes)\n",
                set.value().palette.num_shades, set.value().lookup.alt_palettes.size());
    std::printf("lookup: loaded through VFS (%zu swaps)\n", set.value().lookup.swaps.size());
    // Generic metadata summary for the human attestation (gate A). Counts and
    // dimensions only: no pixel values, no hashes, nothing derived from
    // proprietary content beyond shape. "picanm entries preserved" alone only
    // said a picnum had an ART owner, which establishes nothing about whether
    // the metadata survived -- these lines do.
    std::size_t picanm_entries = 0;
    std::size_t nonzero_pivots = 0;
    std::size_t animated = 0;
    std::size_t nonzero_raw = 0;
    const fauxbuild::AtlasTileEntry* largest = nullptr;
    std::int64_t largest_area = -1;
    for (const auto& tile : a.tiles) {
        if (tile.source.empty()) {
            continue;
        }
        ++picanm_entries;
        if (tile.x_center != 0 || tile.y_center != 0) {
            ++nonzero_pivots;
        }
        if (tile.meta.anim_type != 0 || tile.meta.frames != 0) {
            ++animated;
        }
        if (tile.meta.raw != 0) {
            ++nonzero_raw;
        }
        const std::int64_t area =
            static_cast<std::int64_t>(tile.width) * static_cast<std::int64_t>(tile.height);
        if (tile.populated && area > largest_area) {
            largest_area = area;
            largest = &tile;
        }
    }
    if (largest != nullptr) {
        std::printf("largest populated tile: %dx%d at picnum %u\n", largest->width, largest->height,
                    largest->picnum);
    } else {
        std::printf("largest populated tile: none (no populated tiles)\n");
    }
    std::printf("non-zero pivots: %zu\n", nonzero_pivots);
    std::printf("animated metadata entries: %zu\n", animated);
    std::printf("raw picanm entries preserved: %zu (%zu non-zero)\n", picanm_entries, nonzero_raw);
    std::printf("validation: OK\n");
    return 0;
}

} // namespace fauxbuild::tool
