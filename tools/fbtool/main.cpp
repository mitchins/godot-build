#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "fauxbuild/version.hpp"

namespace {

void print_version() {
    std::printf("fbtool %s (config=%s)\n", fauxbuild::version_string(), fauxbuild::build_config());
}

void print_usage() {
    std::printf(
        "fbtool %s — FauxBuild command-line tool\n"
        "\n"
        "usage: fbtool <command> [args]\n"
        "\n"
        "commands:\n"
        "  --version              print version and build configuration\n"
        "  gen-fixtures [--out DIR]\n"
        "                         (re)generate the empty fixture set\n"
        "  help                   show this message\n"
        "\n"
        "Additional subcommands (dump-map, validate-map, roundtrip-map, dump-art,\n"
        "dump-grp, probe, ...) are added by later milestones.\n",
        fauxbuild::version_string());
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
