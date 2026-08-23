#include "tools/fbtool/build_commands.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "fauxbuild/art.hpp"
#include "fauxbuild/file_io.hpp"
#include "fauxbuild/palette.hpp"
#include "fauxbuild/tile_build.hpp"
#include "fauxbuild/tile_manifest.hpp"

namespace fauxbuild::tool {

namespace {

struct BuildArgs {
    bool usage_error = false;
    bool init_manifest = false;
    std::string source;
    std::string manifest_path;
    std::string out;
    std::string palette_out;
    std::string lookup_out;
};

BuildArgs parse_build_args(int argc, char** argv) {
    BuildArgs args;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            return i + 1 < argc ? std::string(argv[++i]) : std::string();
        };
        if (arg == "--source") {
            args.source = value();
        } else if (arg == "--manifest") {
            args.manifest_path = value();
        } else if (arg == "--out") {
            args.out = value();
        } else if (arg == "--palette-out") {
            args.palette_out = value();
        } else if (arg == "--lookup-out") {
            args.lookup_out = value();
        } else if (arg == "--init-manifest") {
            args.init_manifest = true;
        } else if (!arg.empty() && arg[0] == '-') {
            args.usage_error = true;
            break;
        }
    }
    return args;
}

bool read_text(const std::string& path, std::string& out, const char* what) {
    auto bytes = read_file_bytes(path);
    if (!bytes.is_ok()) {
        std::fprintf(stderr, "fbtool: %s: %s\n", what, bytes.error().to_string().c_str());
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
    return true;
}

} // namespace

int build_art(int argc, char** argv) {
    const BuildArgs args = parse_build_args(argc, argv);
    if (args.usage_error || args.source.empty() || args.out.empty()) {
        std::fprintf(stderr, "fbtool: build-art --source <tileset> --out <art> "
                             "[--manifest <file> | --init-manifest]\n");
        return 2;
    }

    std::string text;
    if (!read_text(args.source, text, "build-art")) {
        return 1;
    }
    auto tileset = parse_tileset(text, args.source);
    if (!tileset.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", tileset.error().to_string().c_str());
        return 1;
    }

    TileManifest manifest;
    if (args.init_manifest) {
        if (!args.manifest_path.empty()) {
            std::fprintf(stderr, "fbtool: build-art: --init-manifest takes no --manifest\n");
            return 2;
        }
    } else {
        if (args.manifest_path.empty()) {
            std::fprintf(stderr, "fbtool: build-art: a stable build needs --manifest <file> "
                                 "(or --init-manifest for the first build)\n");
            return 2;
        }
        std::string manifest_text;
        if (!read_text(args.manifest_path, manifest_text, "build-art")) {
            return 1;
        }
        auto parsed = parse_tile_manifest(manifest_text, args.manifest_path);
        if (!parsed.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", parsed.error().to_string().c_str());
            return 1;
        }
        manifest = parsed.take();
    }

    auto built = build_art_from_tileset(tileset.value(), manifest);
    if (!built.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", built.error().to_string().c_str());
        return 1;
    }

    auto art_bytes = write_art(built.value().art);
    if (!art_bytes.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", art_bytes.error().to_string().c_str());
        return 1;
    }
    auto written = write_file_bytes(args.out, art_bytes.value().data(), art_bytes.value().size());
    if (!written.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", written.error().to_string().c_str());
        return 1;
    }

    auto manifest_text = write_tile_manifest(built.value().manifest);
    if (!manifest_text.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", manifest_text.error().to_string().c_str());
        return 1;
    }
    const std::string manifest_target =
        args.manifest_path.empty() ? args.out + ".manifest" : args.manifest_path;
    auto saved = write_file_bytes(
        manifest_target, reinterpret_cast<const std::uint8_t*>(manifest_text.value().data()),
        manifest_text.value().size());
    if (!saved.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", saved.error().to_string().c_str());
        return 1;
    }

    std::printf("fbtool: built %zu-byte ART (%d tiles, range %d..%d) -> %s\n",
                art_bytes.value().size(), built.value().art.numtiles_field,
                built.value().art.localtilestart, built.value().art.localtileend, args.out.c_str());
    std::printf("fbtool: manifest (%zu tiles) -> %s\n", built.value().manifest.entries.size(),
                manifest_target.c_str());
    return 0;
}

int build_palette(int argc, char** argv) {
    const BuildArgs args = parse_build_args(argc, argv);
    if (args.usage_error || args.source.empty() ||
        (args.palette_out.empty() && args.lookup_out.empty())) {
        std::fprintf(stderr, "fbtool: build-palette --source <spec> "
                             "[--palette-out <file>] [--lookup-out <file>]\n");
        return 2;
    }

    std::string text;
    if (!read_text(args.source, text, "build-palette")) {
        return 1;
    }
    auto spec = parse_palette_spec(text, args.source);
    if (!spec.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", spec.error().to_string().c_str());
        return 1;
    }

    if (!args.palette_out.empty()) {
        auto data = build_palette_dat(spec.value());
        if (!data.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", data.error().to_string().c_str());
            return 1;
        }
        auto bytes = write_palette_dat(data.value());
        if (!bytes.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", bytes.error().to_string().c_str());
            return 1;
        }
        auto saved = write_file_bytes(args.palette_out, bytes.value().data(), bytes.value().size());
        if (!saved.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", saved.error().to_string().c_str());
            return 1;
        }
        std::printf("fbtool: PALETTE.DAT (%zu bytes, %d shades) -> %s\n", bytes.value().size(),
                    data.value().num_shades, args.palette_out.c_str());
    }

    if (!args.lookup_out.empty()) {
        auto data = build_lookup_dat(spec.value());
        if (!data.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", data.error().to_string().c_str());
            return 1;
        }
        auto bytes = write_lookup_dat(data.value());
        if (!bytes.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", bytes.error().to_string().c_str());
            return 1;
        }
        auto saved = write_file_bytes(args.lookup_out, bytes.value().data(), bytes.value().size());
        if (!saved.is_ok()) {
            std::fprintf(stderr, "fbtool: %s\n", saved.error().to_string().c_str());
            return 1;
        }
        std::printf("fbtool: LOOKUP.DAT (%zu bytes, %zu swaps) -> %s\n", bytes.value().size(),
                    data.value().swaps.size(), args.lookup_out.c_str());
    }
    return 0;
}

} // namespace fauxbuild::tool
