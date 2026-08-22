#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "fauxbuild/file_io.hpp"

namespace {

std::filesystem::path temp_dir(const char* tag) {
    const auto dir =
        std::filesystem::temp_directory_path() /
        ("fauxbuild_file_io_" + std::string(tag) + "_" +
         std::to_string(std::filesystem::hash_value(std::filesystem::temp_directory_path() / tag)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("read_file_bytes reports TooLarge instead of truncating") {
    const auto dir = temp_dir("toolarge");
    const auto path = (dir / "big.bin").string();
    const std::vector<std::uint8_t> payload(1024, 0xAB);
    REQUIRE(fauxbuild::write_file_bytes(path, payload.data(), payload.size()).is_ok());

    auto limited = fauxbuild::read_file_bytes(path, 512);
    REQUIRE_FALSE(limited.is_ok());
    CHECK(limited.error().code == fauxbuild::ErrorCode::TooLarge);
    CHECK(limited.error().source == path);

    auto full = fauxbuild::read_file_bytes(path, 1024);
    REQUIRE(full.is_ok());
    CHECK(full.value().size() == 1024);

    std::filesystem::remove_all(dir);
}

TEST_CASE("read_file_bytes fails structurally on a missing file") {
    auto missing = fauxbuild::read_file_bytes("/nonexistent/fauxbuild/missing.bin", 1024);
    REQUIRE_FALSE(missing.is_ok());
    CHECK(missing.error().code == fauxbuild::ErrorCode::IoError);
}

TEST_CASE("write_file_bytes fails structurally on an unwritable path") {
    const std::uint8_t byte = 0;
    auto written = fauxbuild::write_file_bytes("/nonexistent/fauxbuild/out.bin", &byte, 1);
    REQUIRE_FALSE(written.is_ok());
    CHECK(written.error().code == fauxbuild::ErrorCode::IoError);
}

TEST_CASE("zero-byte round trip is exact") {
    const auto dir = temp_dir("empty");
    const auto path = (dir / "empty.bin").string();
    REQUIRE(fauxbuild::write_file_bytes(path, nullptr, 0).is_ok());
    auto back = fauxbuild::read_file_bytes(path, 16);
    REQUIRE(back.is_ok());
    CHECK(back.value().empty());
    std::filesystem::remove_all(dir);
}
