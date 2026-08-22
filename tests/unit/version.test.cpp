#include <cstdio>
#include <cstring>

#include <doctest/doctest.h>

#include "fauxbuild/version.hpp"

TEST_CASE("version string matches declared constants") {
    char expected[32];
    std::snprintf(expected, sizeof(expected), "%d.%d.%d", fauxbuild::kVersionMajor,
                  fauxbuild::kVersionMinor, fauxbuild::kVersionPatch);
    CHECK(std::strcmp(fauxbuild::version_string(), expected) == 0);
}

TEST_CASE("build configuration is one of the known configurations") {
    const char* cfg = fauxbuild::build_config();
    REQUIRE(cfg != nullptr);
    const bool known = std::strcmp(cfg, "dev") == 0 || std::strcmp(cfg, "release") == 0 ||
                       std::strcmp(cfg, "asan") == 0 || std::strcmp(cfg, "fuzz") == 0;
    CHECK(known);
}
