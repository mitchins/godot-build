#include <cstdint>

#include <doctest/doctest.h>

#include "fauxbuild/grp.hpp"
#include "fauxbuild/grp_synth.hpp"

using fauxbuild::grp::parse;
using fauxbuild::synth::generate_grp;
using fauxbuild::synth::GrpSpec;

namespace {

std::string_view view(const std::vector<std::uint8_t>& bytes) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

TEST_CASE("generator is deterministic for identical specs") {
    const GrpSpec spec{.seed = 42, .file_count = 16, .max_file_size = 512};
    const auto a = generate_grp(spec);
    const auto b = generate_grp(spec);
    CHECK(a == b);
}

TEST_CASE("different seeds produce different images") {
    const auto a = generate_grp({.seed = 1, .file_count = 8, .max_file_size = 64});
    const auto b = generate_grp({.seed = 2, .file_count = 8, .max_file_size = 64});
    CHECK(a != b);
}

TEST_CASE("generated images parse cleanly with no warnings") {
    for (std::uint32_t seed = 0; seed < 8; ++seed) {
        const auto image = generate_grp({.seed = seed, .file_count = 9, .max_file_size = 96});
        fauxbuild::grp::GrpDiagnostics diags;
        auto parsed = parse(view(image), "gen", &diags);
        REQUIRE(parsed.is_ok());
        CHECK(diags.warnings.empty());
        CHECK(parsed.value().file_count == 9);
    }
}

TEST_CASE("zero-file spec produces a valid header-only container") {
    const auto image = generate_grp({.seed = 5, .file_count = 0, .max_file_size = 16});
    REQUIRE(image.size() == 16); // 12-byte signature + uint32 count
    auto parsed = parse(view(image), "empty-grp");
    REQUIRE(parsed.is_ok());
    CHECK(parsed.value().entries.empty());
    CHECK(parsed.value().data_start == 16);
}

TEST_CASE("max_file_size at UINT32_MAX does not divide by zero") {
    // Regression: max_file_size + 1 wrapped to zero in uint32, so the size
    // modulo was UB (UBSan: "division by zero") and dev builds passed silently.
    const auto image = generate_grp(
        {.seed = 3, .file_count = 1, .max_file_size = UINT32_MAX, .include_zero_size = true});
    REQUIRE(image.size() >= 16);
    auto parsed = parse(view(image), "uint32-max");
    REQUIRE(parsed.is_ok());
    CHECK(parsed.value().file_count == 1);
}
