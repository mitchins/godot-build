#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "fauxbuild/file_io.hpp"
#include "fauxbuild/grp.hpp"
#include "fauxbuild/grp_synth.hpp"
#include "fauxbuild/version.hpp"

namespace {

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
                "  gen-grp --out FILE [--seed N] [--files N] [--max-size N]\n"
                "                         write a deterministic synthetic GRP\n"
                "  gen-fixtures [--out DIR]\n"
                "                         (re)generate the empty fixture set\n"
                "  help                   show this message\n"
                "\n"
                "Additional subcommands (dump-map, validate-map, roundtrip-map, dump-art,\n"
                "probe, ...) are added by later milestones.\n",
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
    std::printf("files: %u (declared data length: %u bytes)\n", grp.file_count, grp.data_length);
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
            spec.seed = static_cast<std::uint32_t>(std::strtoul(value().c_str(), nullptr, 10));
        } else if (arg == "--files") {
            spec.file_count =
                static_cast<std::uint32_t>(std::strtoul(value().c_str(), nullptr, 10));
        } else if (arg == "--max-size") {
            spec.max_file_size =
                static_cast<std::uint32_t>(std::strtoul(value().c_str(), nullptr, 10));
        } else {
            std::fprintf(stderr, "fbtool: gen-grp: unknown argument '%s'\n", arg.c_str());
            return 2;
        }
    }
    if (out.empty()) {
        std::fprintf(stderr, "fbtool: gen-grp: --out is required\n");
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
    if (std::strcmp(cmd, "gen-grp") == 0) {
        return gen_grp(argc - 2, argv + 2);
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
