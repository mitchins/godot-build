#include "tools/fbtool/map_commands.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fauxbuild/file_io.hpp"
#include "fauxbuild/map_diff.hpp"
#include "fauxbuild/map_io.hpp"
#include "fauxbuild/map_synth.hpp"
#include "fauxbuild/map_validate.hpp"
#include "fauxbuild/vfs.hpp"

namespace fauxbuild::tool {

namespace {

struct MapArgs {
    bool verbose = false;
    bool usage_error = false;
    std::string grp;
    std::vector<std::string> positional;
};

bool looks_like_option(const std::string& arg) {
    return arg.size() > 1 && arg[0] == '-';
}

// Options are per-command: only commands that render output accept --verbose,
// so `validate-map --verbose` is a usage error rather than a silently ignored
// flag. Anything unrecognised, and any option-looking token where a value is
// expected, is a usage error (exit 2) — never a positional path, which would
// surface as an exit-1 content error against a file that does not exist.
MapArgs parse_args(int argc, char** argv, bool allow_verbose) {
    MapArgs args;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (allow_verbose && (arg == "--verbose" || arg == "-v")) {
            args.verbose = true;
        } else if (arg == "--grp") {
            if (i + 1 >= argc || looks_like_option(argv[i + 1])) {
                args.usage_error = true;
                break;
            }
            args.grp = argv[++i];
        } else if (looks_like_option(arg)) {
            args.usage_error = true;
            break;
        } else {
            args.positional.push_back(arg);
        }
    }
    return args;
}

Result<mapv7::MapData> load_map(const std::string& grp, const std::string& name) {
    if (grp.empty()) {
        auto bytes = read_file_bytes(name);
        if (!bytes.is_ok()) {
            return Result<mapv7::MapData>::err(bytes.error());
        }
        const std::string_view view(reinterpret_cast<const char*>(bytes.value().data()),
                                    bytes.value().size());
        return read_map(view, name);
    }
    auto mount = GrpMount::create(grp);
    if (!mount.is_ok()) {
        return Result<mapv7::MapData>::err(mount.error());
    }
    Vfs vfs;
    vfs.add_mount(mount.take());
    auto file = vfs.open(name);
    if (!file.is_ok()) {
        return Result<mapv7::MapData>::err(file.error());
    }
    const std::string_view view(reinterpret_cast<const char*>(file.value().bytes.data()),
                                file.value().bytes.size());
    return read_map(view, grp + ":" + file.value().name);
}

bool print_report(const ValidationReport& report) {
    for (const auto& issue : report.issues) {
        std::printf("  [%s] %s %s: %s\n", issue.severity == Severity::Error ? "error" : "warning",
                    error_code_name(issue.code), issue.record.c_str(), issue.detail.c_str());
    }
    if (report.truncated) {
        std::printf("  ... (issue cap reached; more problems may exist)\n");
    }
    std::printf("validation: %s (%zu errors, %zu warnings)\n", report.ok() ? "OK" : "FAILED",
                report.error_count(), report.warning_count());
    return report.ok();
}

} // namespace

int dump_map(int argc, char** argv) {
    const MapArgs args = parse_args(argc, argv, /*allow_verbose=*/true);
    if (args.usage_error || args.positional.size() != 1) {
        std::fprintf(stderr, "fbtool: dump-map [--grp FILE] <map-path-or-name> [--verbose]\n");
        return 2;
    }
    auto map = load_map(args.grp, args.positional[0]);
    if (!map.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", map.error().to_string().c_str());
        return 1;
    }
    const auto& world = map.value();

    std::size_t portal_walls = 0;
    std::size_t masked_walls = 0;
    std::size_t face_sprites = 0;
    std::size_t wall_sprites = 0;
    std::size_t floor_sprites = 0;
    std::size_t other_sprites = 0;
    for (const auto& wall : world.walls) {
        if (wall.nextwall != mapv7::kNoIndex) {
            ++portal_walls;
        }
        if ((wall.cstat & 0x0002) != 0) {
            ++masked_walls;
        }
    }
    for (const auto& sprite : world.sprites) {
        if ((sprite.cstat & 0x0010) != 0) {
            ++floor_sprites;
        } else if ((sprite.cstat & 0x0008) != 0) {
            ++wall_sprites;
        } else {
            ++face_sprites;
        }
    }

    std::printf("source: %s\n", world.source.c_str());
    std::printf("version: 7\n");
    std::printf("start: x=%d y=%d z=%d angle=%d sector=%d\n", world.start.x, world.start.y,
                world.start.z, world.start.angle, world.start.sector);
    std::printf("sectors: %zu  walls: %zu  sprites: %zu\n", world.sectors.size(),
                world.walls.size(), world.sprites.size());
    std::printf("portal walls: %zu  masked-flag walls (cstat&2): %zu\n", portal_walls,
                masked_walls);
    std::printf("sprites: face=%zu wall-aligned(cstat&8)=%zu floor-aligned(cstat&16)=%zu "
                "other=%zu\n",
                face_sprites, wall_sprites, floor_sprites, other_sprites);

    std::printf("sector wall ranges:\n");
    const std::size_t shown =
        args.verbose ? world.sectors.size() : std::min<std::size_t>(world.sectors.size(), 8);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& sector = world.sectors[i];
        std::printf("  sector[%zu]: walls [%d, %d) floorz=%d ceilingz=%d\n", i, sector.wallptr,
                    sector.wallptr + sector.wallnum, sector.floorz, sector.ceilingz);
    }
    if (!args.verbose && world.sectors.size() > shown) {
        std::printf("  ... (%zu more; --verbose shows all)\n", world.sectors.size() - shown);
    }

    if (args.verbose) {
        for (std::size_t i = 0; i < world.walls.size(); ++i) {
            const auto& w = world.walls[i];
            std::printf("  wall[%zu]: (%d,%d) point2=%d nextwall=%d nextsector=%d cstat=%d "
                        "picnum=%d overpicnum=%d\n",
                        i, w.x, w.y, w.point2, w.nextwall, w.nextsector, w.cstat, w.picnum,
                        w.overpicnum);
        }
        for (std::size_t i = 0; i < world.sprites.size(); ++i) {
            const auto& s = world.sprites[i];
            std::printf("  sprite[%zu]: (%d,%d,%d) cstat=%d picnum=%d sectnum=%d "
                        "statnum=%d ang=%d\n",
                        i, s.x, s.y, s.z, s.cstat, s.picnum, s.sectnum, s.statnum, s.ang);
        }
    }

    print_report(fauxbuild::validate_map(world));
    return 0;
}

int validate_map(int argc, char** argv) {
    const MapArgs args = parse_args(argc, argv, /*allow_verbose=*/false);
    if (args.usage_error || args.positional.size() != 1) {
        std::fprintf(stderr, "fbtool: validate-map [--grp FILE] <map-path-or-name>\n");
        return 2;
    }
    auto map = load_map(args.grp, args.positional[0]);
    if (!map.is_ok()) {
        std::fprintf(stderr, "fbtool: parse: %s\n", map.error().to_string().c_str());
        return 1;
    }
    return print_report(fauxbuild::validate_map(map.value())) ? 0 : 1;
}

int rewrite_map(int argc, char** argv) {
    const MapArgs args = parse_args(argc, argv, /*allow_verbose=*/false);
    if (args.usage_error || args.positional.size() != 2) {
        std::fprintf(stderr, "fbtool: rewrite-map [--grp FILE] <in> <out>\n");
        return 2;
    }
    auto map = load_map(args.grp, args.positional[0]);
    if (!map.is_ok()) {
        std::fprintf(stderr, "fbtool: parse: %s\n", map.error().to_string().c_str());
        return 1;
    }
    auto bytes = write_map(map.value());
    if (!bytes.is_ok()) {
        std::fprintf(stderr, "fbtool: write: %s\n", bytes.error().to_string().c_str());
        return 1;
    }
    auto written = write_file_bytes(args.positional[1], bytes.value().data(), bytes.value().size());
    if (!written.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", written.error().to_string().c_str());
        return 1;
    }
    // Self-check: the rewrite must parse and be semantically identical.
    auto reparsed = read_map(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()),
        "rewrite");
    if (!reparsed.is_ok() || !diff_maps(map.value(), reparsed.value()).identical) {
        std::fprintf(stderr, "fbtool: rewrite self-check failed\n");
        return 1;
    }
    std::printf("fbtool: rewrote %s -> %s (%zu bytes, semantic diff empty)\n",
                args.positional[0].c_str(), args.positional[1].c_str(), bytes.value().size());
    return 0;
}

int diff_map(int argc, char** argv) {
    const MapArgs args = parse_args(argc, argv, /*allow_verbose=*/false);
    if (args.usage_error || args.positional.size() != 2) {
        std::fprintf(stderr, "fbtool: diff-map [--grp FILE] <a> <b>\n");
        return 2;
    }
    auto a = load_map(args.grp, args.positional[0]);
    if (!a.is_ok()) {
        std::fprintf(stderr, "fbtool: %s: %s\n", args.positional[0].c_str(),
                     a.error().to_string().c_str());
        return 1;
    }
    auto b = load_map(args.grp, args.positional[1]);
    if (!b.is_ok()) {
        std::fprintf(stderr, "fbtool: %s: %s\n", args.positional[1].c_str(),
                     b.error().to_string().c_str());
        return 1;
    }
    const MapDiff diff = diff_maps(a.value(), b.value());
    if (diff.identical) {
        std::printf("maps are semantically identical\n");
        return 0;
    }
    for (const auto& note : diff.notes) {
        std::printf("  %s\n", note.c_str());
    }
    if (diff.notes.size() >= 64) {
        std::printf("  ... (note cap reached)\n");
    }
    std::printf("maps differ\n");
    return 1;
}

int gen_map(int argc, char** argv) {
    std::string fixture;
    std::string out;
    bool list = false;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--fixture" && i + 1 < argc) {
            fixture = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out = argv[++i];
        } else if (arg == "--list") {
            list = true;
        } else {
            std::fprintf(stderr, "fbtool: gen-map: unknown argument '%s'\n", arg.c_str());
            return 2;
        }
    }
    if (list) {
        if (!fixture.empty() || !out.empty()) {
            std::fprintf(stderr, "fbtool: gen-map --list takes no other arguments\n");
            return 2;
        }
        for (const auto& name : synth::map_fixture_names()) {
            std::printf("%s\n", name.c_str());
        }
        return 0;
    }
    if (fixture.empty() || out.empty()) {
        std::fprintf(stderr, "fbtool: gen-map --fixture <name> --out <file> | --list\n");
        return 2;
    }
    auto bytes = synth::serialize_map_fixture(fixture);
    if (!bytes.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", bytes.error().to_string().c_str());
        return 1;
    }
    auto written = write_file_bytes(out, bytes.value().data(), bytes.value().size());
    if (!written.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", written.error().to_string().c_str());
        return 1;
    }
    std::printf("fbtool: wrote %zu-byte fixture '%s' to %s\n", bytes.value().size(),
                fixture.c_str(), out.c_str());
    return 0;
}

} // namespace fauxbuild::tool
