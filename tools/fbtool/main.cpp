#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "fauxbuild/file_io.hpp"
#include "fauxbuild/grp.hpp"
#include "fauxbuild/grp_synth.hpp"
#include "fauxbuild/version.hpp"
#include "fauxbuild/vfs.hpp"
#include "tools/fbtool/art_commands.hpp"
#include "tools/fbtool/build_commands.hpp"
#include "tools/fbtool/map_commands.hpp"
#include "tools/fbtool/palette_commands.hpp"

namespace {

// Synthetic-generator bounds. These cap what the CLI will ask generate_grp to
// build; they keep a fat-fingered argument from requesting a multi-gigabyte
// allocation and are well above anything a fixture needs.
constexpr std::uint32_t kMaxSynthFiles = 65536;
constexpr std::uint32_t kMaxSynthFileSize = 16u * 1024u * 1024u;
// The per-option bounds are independently reasonable but multiply: 65536 files
// at 16 MiB each is a ~1 TiB request that passes both. Fixtures never need
// anything near this, so bound the product too.
constexpr std::uint64_t kMaxSynthPayload = 256ull * 1024ull * 1024ull;

// Strict unsigned option parsing: strtoul accepts an empty string, ignores
// trailing garbage ("12x"), and its result wraps when narrowed. Every numeric
// option is validated before it reaches GrpSpec.
bool parse_u32_option(const std::string& text, std::uint32_t max_value, const char* option,
                      std::uint32_t& out) {
    if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0]))) {
        std::fprintf(stderr, "fbtool: gen-grp: %s expects a non-negative integer, got '%s'\n",
                     option, text.c_str());
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long raw = std::strtoull(text.c_str(), &end, 10);
    if (errno == ERANGE || end == nullptr || *end != '\0') {
        std::fprintf(stderr, "fbtool: gen-grp: %s expects a non-negative integer, got '%s'\n",
                     option, text.c_str());
        return false;
    }
    if (raw > max_value) {
        std::fprintf(stderr, "fbtool: gen-grp: %s must be <= %u, got %s\n", option, max_value,
                     text.c_str());
        return false;
    }
    out = static_cast<std::uint32_t>(raw);
    return true;
}

void print_version() {
    std::printf("fbtool %s (config=%s)\n", fauxbuild::version_string(), fauxbuild::build_config());
}

void print_usage() {
    std::printf("fbtool %s — FauxBuild command-line tool\n"
                "\n"
                "usage: fbtool <command> [args]\n"
                "\n"
                "commands:\n"
                "  --version              print version and build configuration\n"
                "  dump-grp <file.grp>    parse a GRP container and list its directory\n"
                "  dump-map [--grp G] <m> parse a MAP v7 and summarize the world\n"
                "  validate-map [--grp G] <m>  structural validation report\n"
                "  rewrite-map [--grp G] <in> <out>  canonical rewrite + self-check\n"
                "  diff-map [--grp G] <a> <b>  semantic field diff of two maps\n"
                "  gen-map --fixture N --out F | --list   synthetic MAP fixtures\n"
                "  dump-palette [--grp G] <f>   parse PALETTE.DAT and summarize\n"
                "  dump-lookup [--grp G] <f>    parse LOOKUP.DAT and summarize\n"
                "  dump-art [--grp G] <f>      parse an ART container and summarize\n"
                "  build-art --source S --out F [--manifest M|--init-manifest]\n"
                "                              compile a tileset into ART + manifest\n"
                "  build-palette --source S [--palette-out F] [--lookup-out F]\n"
                "                              compile a palette spec\n"
                "  gen-grp --out FILE [--seed N] [--files N] [--max-size N]\n"
                "                         write a deterministic synthetic GRP\n"
                "  gen-fixtures [--out DIR]\n"
                "                         (re)generate the empty fixture set\n"
                "  help                   show this message\n"
                "\n"
                "Additional subcommands (dump-art, probe, ...) are added by later\n"
                "milestones.\n",
                fauxbuild::version_string());
}

int dump_grp(int argc, char** argv) {
    if (argc != 1) {
        std::fprintf(stderr, "fbtool: dump-grp: expected exactly one GRP path\n");
        return 2;
    }
    const std::string path = argv[0];
    auto image = fauxbuild::read_file_bytes(path);
    if (!image.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", image.error().to_string().c_str());
        return 1;
    }
    fauxbuild::grp::GrpDiagnostics diags;
    const std::string_view view(reinterpret_cast<const char*>(image.value().data()),
                                image.value().size());
    auto data = fauxbuild::grp::parse(view, path, &diags);
    if (!data.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", data.error().to_string().c_str());
        return 1;
    }
    const auto& grp = data.value();
    std::printf("source: %s\n", path.c_str());
    std::printf("files: %u (data starts at offset %llu)\n", grp.file_count,
                static_cast<unsigned long long>(grp.data_start));
    std::printf("idx  offset      size  name\n");
    for (std::size_t i = 0; i < grp.entries.size(); ++i) {
        const auto& entry = grp.entries[i];
        std::printf("%3zu  %10llu  %6u  %s\n", i, static_cast<unsigned long long>(entry.offset),
                    entry.size, entry.name.c_str());
    }
    for (const auto& warning : diags.warnings) {
        std::printf("warning: %s\n", warning.c_str());
    }
    return 0;
}

int gen_grp(int argc, char** argv) {
    std::string out;
    fauxbuild::synth::GrpSpec spec;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            return i + 1 < argc ? std::string(argv[++i]) : std::string();
        };
        if (arg == "--out") {
            out = value();
        } else if (arg == "--seed") {
            if (!parse_u32_option(value(), UINT32_MAX, "--seed", spec.seed)) {
                return 2;
            }
        } else if (arg == "--files") {
            if (!parse_u32_option(value(), kMaxSynthFiles, "--files", spec.file_count)) {
                return 2;
            }
        } else if (arg == "--max-size") {
            if (!parse_u32_option(value(), kMaxSynthFileSize, "--max-size", spec.max_file_size)) {
                return 2;
            }
        } else {
            std::fprintf(stderr, "fbtool: gen-grp: unknown argument '%s'\n", arg.c_str());
            return 2;
        }
    }
    if (out.empty()) {
        std::fprintf(stderr, "fbtool: gen-grp: --out is required\n");
        return 2;
    }
    const std::uint64_t worst_case =
        static_cast<std::uint64_t>(spec.file_count) * spec.max_file_size;
    if (worst_case > kMaxSynthPayload) {
        std::fprintf(stderr,
                     "fbtool: gen-grp: --files %u x --max-size %u could generate %llu bytes, "
                     "over the %llu-byte limit\n",
                     spec.file_count, spec.max_file_size,
                     static_cast<unsigned long long>(worst_case),
                     static_cast<unsigned long long>(kMaxSynthPayload));
        return 2;
    }
    const auto bytes = fauxbuild::synth::generate_grp(spec);
    auto written = fauxbuild::write_file_bytes(out, bytes.data(), bytes.size());
    if (!written.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", written.error().to_string().c_str());
        return 1;
    }
    std::printf("fbtool: wrote %zu-byte synthetic GRP to %s (seed=%u files=%u)\n", bytes.size(),
                out.c_str(), spec.seed, spec.file_count);
    return 0;
}

// Reads named entries through the mounted-file API rather than the directory.
// dump-grp parses the container directly; this exercises GrpMount + the
// case-normalized VFS lookup and confirms each file reads to its declared size.
int vfs_stat(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "fbtool: vfs-stat: usage: vfs-stat <grp> <name> [name...]\n");
        return 2;
    }
    const std::string path = argv[0];
    fauxbuild::grp::GrpDiagnostics grp_diags;
    auto mount = fauxbuild::GrpMount::create(path, &grp_diags);
    if (!mount.is_ok()) {
        std::fprintf(stderr, "fbtool: %s\n", mount.error().to_string().c_str());
        return 1;
    }

    fauxbuild::Vfs vfs;
    vfs.add_mount(mount.take());
    std::printf("mounted: %s (%zu mount)\n", path.c_str(), vfs.mount_count());
    for (const auto& warning : grp_diags.warnings) {
        std::printf("warning: %s\n", warning.c_str());
    }

    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string query = argv[i];
        auto file = vfs.open(query);
        if (!file.is_ok()) {
            std::printf("%-14s  MISS  %s\n", query.c_str(), file.error().to_string().c_str());
            ++failures;
            continue;
        }
        const auto& f = file.value();
        // Read-to-EOF check: the mounted file must deliver exactly its size.
        const bool exact = f.bytes.size() == f.size;
        std::printf("%-14s  %s  %llu bytes  via %s\n", query.c_str(), exact ? "OK  " : "SHORT",
                    static_cast<unsigned long long>(f.size), f.origin.c_str());
        if (!exact) {
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

int gen_fixtures(int argc, char** argv) {
    std::filesystem::path out = "fixtures/generated";
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "fbtool: gen-fixtures: --out requires a path\n");
                return 2;
            }
            out = argv[i + 1];
            ++i;
        } else {
            std::fprintf(stderr, "fbtool: gen-fixtures: unknown argument '%s'\n", argv[i]);
            return 2;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    if (ec) {
        std::fprintf(stderr, "fbtool: gen-fixtures: cannot create %s: %s\n", out.string().c_str(),
                     ec.message().c_str());
        return 1;
    }

    const std::filesystem::path manifest = out / "MANIFEST.txt";
    std::FILE* f = std::fopen(manifest.string().c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "fbtool: gen-fixtures: cannot write %s\n", manifest.string().c_str());
        return 1;
    }
    std::fprintf(f,
                 "# FauxBuild generated fixtures\n"
                 "# generator: fbtool %s (config=%s)\n"
                 "# This file is reproducible tool output. Do not hand-edit.\n"
                 "# Fixture generation begins at milestone M3 (MAP v7).\n"
                 "fixtures: none\n",
                 fauxbuild::version_string(), fauxbuild::build_config());
    std::fclose(f);

    std::printf("fbtool: wrote empty fixture manifest to %s\n", manifest.string().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }

    const char* cmd = argv[1];
    if (std::strcmp(cmd, "--version") == 0 || std::strcmp(cmd, "version") == 0) {
        print_version();
        return 0;
    }
    if (std::strcmp(cmd, "dump-grp") == 0) {
        return dump_grp(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "vfs-stat") == 0) {
        return vfs_stat(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "gen-grp") == 0) {
        return gen_grp(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "dump-map") == 0) {
        return fauxbuild::tool::dump_map(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "validate-map") == 0) {
        return fauxbuild::tool::validate_map(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "rewrite-map") == 0) {
        return fauxbuild::tool::rewrite_map(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "diff-map") == 0) {
        return fauxbuild::tool::diff_map(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "gen-map") == 0) {
        return fauxbuild::tool::gen_map(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "dump-palette") == 0) {
        return fauxbuild::tool::dump_palette(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "dump-lookup") == 0) {
        return fauxbuild::tool::dump_lookup(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "dump-art") == 0) {
        return fauxbuild::tool::dump_art(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "build-art") == 0) {
        return fauxbuild::tool::build_art(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "build-palette") == 0) {
        return fauxbuild::tool::build_palette(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "gen-fixtures") == 0) {
        return gen_fixtures(argc - 2, argv + 2);
    }
    if (std::strcmp(cmd, "help") == 0 || std::strcmp(cmd, "--help") == 0 ||
        std::strcmp(cmd, "-h") == 0) {
        print_usage();
        return 0;
    }

    std::fprintf(stderr, "fbtool: unknown command '%s'\n\n", cmd);
    print_usage();
    return 2;
}
