#include "tools/fbtool/art_commands.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fauxbuild/art.hpp"
#include "fauxbuild/file_io.hpp"
#include "fauxbuild/vfs.hpp"

namespace fauxbuild::tool {

namespace {

struct Args {
    bool verbose = false;
    bool usage_error = false;
    std::string grp;
    std::vector<std::string> positional;
};

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            args.verbose = true;
        } else if (arg == "--grp") {
            if (i + 1 >= argc) {
                args.usage_error = true;
                break;
            }
            args.grp = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            args.usage_error = true;
            break;
        } else {
            args.positional.push_back(arg);
        }
    }
    return args;
}

} // namespace

// Output policy (M4 brief, provenance): generic statistics only by default.
// --verbose shows dims/metadata for the first few tiles; terminal output of
// real content is fine, committing it anywhere is not.
int dump_art(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    if (args.usage_error || args.positional.size() != 1) {
        std::fprintf(stderr, "fbtool: dump-art [--grp FILE] <tiles.art> [--verbose]\n");
        return 2;
    }

    std::vector<std::uint8_t> bytes;
    if (args.grp.empty()) {
        auto loaded = read_file_bytes(args.positional[0]);
        if (!loaded.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", loaded.error().to_string().c_str());
            return 1;
        }
        bytes = loaded.take();
    } else {
        auto mount = GrpMount::create(args.grp);
        if (!mount.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", mount.error().to_string().c_str());
            return 1;
        }
        Vfs vfs;
        vfs.add_mount(mount.take());
        auto file = vfs.open(args.positional[0]);
        if (!file.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", file.error().to_string().c_str());
            return 1;
        }
        bytes = std::move(file.value().bytes);
    }

    const std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    auto parsed = read_art(view, args.positional[0]);
    if (!parsed.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", parsed.error().to_string().c_str());
        return 1;
    }
    const auto& art = parsed.value();

    std::size_t max_w = 0, max_h = 0, zero_dim = 0, animated = 0, pixel_bytes = 0;
    std::size_t largest_area = 0;
    std::size_t largest_index = 0;
    unsigned anim_types[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < art.tiles.size(); ++i) {
        const auto& tile = art.tiles[i];
        max_w = std::max<std::size_t>(max_w, tile.width);
        max_h = std::max<std::size_t>(max_h, tile.height);
        const std::size_t area =
            static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height);
        if (area > largest_area) {
            largest_area = area;
            largest_index = i;
        }
        if (tile.width == 0 || tile.height == 0) {
            ++zero_dim;
        }
        if (tile.meta.anim_type != 0) {
            ++animated;
        }
        ++anim_types[tile.meta.anim_type & 0x3];
        pixel_bytes += tile.pixels.size();
    }

    std::printf("source: %s\n", art.source.c_str());
    std::printf("version: %d\n", art.version);
    std::printf("tile range: %d..%d (%zu tiles; numtiles field: %d, global)\n", art.localtilestart,
                art.localtileend, art.tiles.size(), art.numtiles_field);
    std::printf("dims: max width %zu, max height %zu (independent maxima); largest tile "
                "%dx%d at [%zu]; zero-dimension tiles: %zu\n",
                max_w, max_h, art.tiles.empty() ? 0 : art.tiles[largest_index].width,
                art.tiles.empty() ? 0 : art.tiles[largest_index].height, largest_index, zero_dim);
    std::printf("animated tiles: %zu (none=%u osc=%u fwd=%u back=%u)\n", animated, anim_types[0],
                anim_types[1], anim_types[2], anim_types[3]);
    std::printf("pixel bytes: %zu (stored verbatim in file order; ordering per published\n"
                "description, not independently verified)\n",
                pixel_bytes);
    std::printf("source hash: %016llx\n", static_cast<unsigned long long>(art.source_hash));

    if (args.verbose) {
        const std::size_t shown = std::min<std::size_t>(art.tiles.size(), 8);
        for (std::size_t i = 0; i < shown; ++i) {
            const auto& tile = art.tiles[i];
            std::printf("  tile[%zu]: %dx%d frames=%u type=%u speed=%u center=(%d,%d)\n", i,
                        tile.width, tile.height, tile.meta.frames, tile.meta.anim_type,
                        tile.meta.speed, tile.meta.x_center, tile.meta.y_center);
        }
        if (art.tiles.size() > shown) {
            std::printf("  ... (%zu more)\n", art.tiles.size() - shown);
        }
    }
    return 0;
}

} // namespace fauxbuild::tool
