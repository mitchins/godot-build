#include "tools/fbtool/palette_commands.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fauxbuild/file_io.hpp"
#include "fauxbuild/palette.hpp"
#include "fauxbuild/vfs.hpp"

namespace fauxbuild::tool {

namespace {

struct Args {
    bool usage_error = false;
    std::string grp;
    std::vector<std::string> positional;
};

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--grp") {
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

Result<std::vector<std::uint8_t>> load_bytes(const std::string& grp, const std::string& name) {
    if (grp.empty()) {
        return read_file_bytes(name);
    }
    auto mount = GrpMount::create(grp);
    if (!mount.is_ok()) {
        return Result<std::vector<std::uint8_t>>::err(mount.error());
    }
    Vfs vfs;
    vfs.add_mount(mount.take());
    auto file = vfs.open(name);
    if (!file.is_ok()) {
        return Result<std::vector<std::uint8_t>>::err(file.error());
    }
    return Result<std::vector<std::uint8_t>>::ok(std::move(file.value().bytes));
}

std::string_view view_of(const std::vector<std::uint8_t>& bytes) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

int dump_palette(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    if (args.usage_error || args.positional.size() != 1) {
        std::fprintf(stderr, "fbtool: dump-palette [--grp FILE] <palette.dat>\n");
        return 2;
    }
    auto bytes = load_bytes(args.grp, args.positional[0]);
    if (!bytes.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", bytes.error().to_string().c_str());
        return 1;
    }
    auto data = read_palette_dat(view_of(bytes.value()), args.positional[0]);
    if (!data.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", data.error().to_string().c_str());
        return 1;
    }
    const auto& p = data.value();

    unsigned max_component = 0;
    for (const auto b : p.rgb) {
        max_component = std::max(max_component, static_cast<unsigned>(b));
    }
    std::printf("source: %s\n", p.source.c_str());
    std::printf("palette: 256 entries, component range 0..%u%s\n", max_component,
                max_component <= 63 ? " (6-bit VGA)" : " (EXCEEDS 6-bit!)");
    std::printf("shade tables: %d declared (%zu bytes)\n", p.num_shades, p.shade_tables.size());
    std::printf("extra tables: %zu bytes (%zu tables, undeclared in-band; preserved)\n",
                p.extra_tables.size(), p.extra_tables.size() / 256);
    std::printf("translucency: %zu bytes\n", p.translucency.size());
    std::printf("source hash: %016llx\n", static_cast<unsigned long long>(p.source_hash));
    return 0;
}

int dump_lookup(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    if (args.usage_error || args.positional.size() != 1) {
        std::fprintf(stderr, "fbtool: dump-lookup [--grp FILE] <lookup.dat>\n");
        return 2;
    }
    auto bytes = load_bytes(args.grp, args.positional[0]);
    if (!bytes.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", bytes.error().to_string().c_str());
        return 1;
    }
    auto data = read_lookup_dat(view_of(bytes.value()), args.positional[0]);
    if (!data.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", data.error().to_string().c_str());
        return 1;
    }
    const auto& l = data.value();

    std::printf("source: %s\n", l.source.c_str());
    std::printf("swaps: %zu (indices:", l.swaps.size());
    for (const auto& swap : l.swaps) {
        std::printf(" %u", swap.index);
    }
    std::printf(")\n");
    std::printf("alt palettes: %zu\n", l.alt_palettes.size());
    std::printf("source hash: %016llx\n", static_cast<unsigned long long>(l.source_hash));
    return 0;
}

} // namespace fauxbuild::tool
