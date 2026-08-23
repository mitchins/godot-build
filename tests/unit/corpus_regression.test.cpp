// D0010(c): every committed fuzz input (seed corpus + regression crashers)
// is exercised by the ordinary check suite, so regressions fail even on
// platforms/configurations without the fuzz runtime. This file must never
// be removed without replacing the guarantee.
#include <algorithm>
#include <filesystem>
#include <fstream>

#include <doctest/doctest.h>

#include "fauxbuild/art.hpp"
#include "fauxbuild/grp.hpp"
#include "fauxbuild/map_io.hpp"
#include "fauxbuild/map_validate.hpp"
#include "fauxbuild/palette.hpp"

namespace {

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

std::vector<std::filesystem::path> collect(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(dir)) {
        return files;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

TEST_CASE("committed fuzz corpus and regression inputs parse without crashing") {
    const std::filesystem::path tests_dir =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path fuzz_dir = tests_dir / "fuzz";

    int parsed_ok = 0;
    int parsed_err = 0;
    int files = 0;
    for (const char* group : {"corpus/grp", "regression/grp"}) {
        for (const auto& path : collect(fuzz_dir / group)) {
            ++files;
            const auto bytes = read_bytes(path);
            const std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            auto result = fauxbuild::grp::parse(view, path.filename().string());
            if (result.is_ok()) {
                ++parsed_ok;
                for (const auto& entry : result.value().entries) {
                    CHECK(entry.offset + entry.size <= bytes.size());
                }
            } else {
                ++parsed_err;
                CHECK(result.error().offset <= bytes.size() + 1);
            }
        }
    }

    // The seed corpus must exist and be non-trivial (D0010(b)).
    CHECK(files >= 6);
    CHECK(parsed_ok >= 2);
    CHECK(parsed_err >= 2);
}

TEST_CASE("committed MAP corpus and regression inputs parse without crashing") {
    const std::filesystem::path tests_dir =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path fuzz_dir = tests_dir / "fuzz";

    int parsed_ok = 0;
    int parsed_err = 0;
    int files = 0;
    for (const char* group : {"corpus/map", "regression/map"}) {
        for (const auto& path : collect(fuzz_dir / group)) {
            ++files;
            const auto bytes = read_bytes(path);
            const std::string_view view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            auto result = fauxbuild::read_map(view, path.filename().string());
            if (result.is_ok()) {
                ++parsed_ok;
                // Valid corpus maps must also validate within bounds.
                const auto report = fauxbuild::validate_map(result.value());
                CHECK(report.issues.size() <= 64 + 1);
            } else {
                ++parsed_err;
                CHECK(result.error().offset <= bytes.size() + 1);
            }
        }
    }

    CHECK(files >= 6);
    CHECK(parsed_ok >= 3);
    CHECK(parsed_err >= 2);
}

TEST_CASE("committed palette corpus and regression inputs parse without crashing") {
    const std::filesystem::path tests_dir =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path fuzz_dir = tests_dir / "fuzz";

    int parsed_ok = 0;
    int parsed_err = 0;
    int files = 0;
    for (const char* group : {"corpus/palette", "regression/palette"}) {
        for (const auto& path : collect(fuzz_dir / group)) {
            ++files;
            const auto bytes = read_bytes(path);
            const std::string_view v(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            auto palette = fauxbuild::read_palette_dat(v, path.filename().string());
            auto lookup = fauxbuild::read_lookup_dat(v, path.filename().string());
            if (palette.is_ok()) {
                ++parsed_ok;
                auto written = fauxbuild::write_palette_dat(palette.value());
                REQUIRE(written.is_ok());
                CHECK(written.value().size() == bytes.size());
            } else if (lookup.is_ok()) {
                ++parsed_ok;
            } else {
                ++parsed_err;
            }
        }
    }

    CHECK(files >= 6);
    CHECK(parsed_ok >= 3);
    CHECK(parsed_err >= 2);
}

TEST_CASE("committed ART corpus and regression inputs parse without crashing") {
    const std::filesystem::path tests_dir =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path fuzz_dir = tests_dir / "fuzz";

    int parsed_ok = 0;
    int parsed_err = 0;
    int files = 0;
    for (const char* group : {"corpus/art", "regression/art"}) {
        for (const auto& path : collect(fuzz_dir / group)) {
            ++files;
            const auto bytes = read_bytes(path);
            const std::string_view v(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            auto art = fauxbuild::read_art(v, path.filename().string());
            if (art.is_ok()) {
                ++parsed_ok;
                auto written = fauxbuild::write_art(art.value());
                REQUIRE(written.is_ok());
                CHECK(written.value() == bytes); // verbatim round-trip
            } else {
                ++parsed_err;
            }
        }
    }

    CHECK(files >= 7);
    CHECK(parsed_ok >= 3);
    CHECK(parsed_err >= 2);
}
