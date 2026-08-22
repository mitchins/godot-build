#include <chrono>
#include <cstring>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

#include "fauxbuild/grp_synth.hpp"
#include "fauxbuild/vfs.hpp"

using fauxbuild::DirectoryMount;
using fauxbuild::ErrorCode;
using fauxbuild::GrpMount;
using fauxbuild::MemoryMount;
using fauxbuild::normalize_vfs_name;
using fauxbuild::Vfs;

namespace {

std::vector<std::uint8_t> bytes_of(const char* text) {
    return std::vector<std::uint8_t>(text, text + std::strlen(text));
}

} // namespace

TEST_CASE("name normalization: case-folded, flat, traversal rejected") {
    auto ok = normalize_vfs_name("a.Map");
    REQUIRE(ok.is_ok());
    CHECK(ok.value() == "A.MAP");

    for (const char* traversal : {".", "..", "a/b", "a\\b", "../x"}) {
        auto rejected = normalize_vfs_name(traversal);
        REQUIRE_FALSE(rejected.is_ok());
        CHECK(rejected.error().code == ErrorCode::PathTraversal);
    }
    auto empty = normalize_vfs_name("");
    REQUIRE_FALSE(empty.is_ok());
    CHECK(empty.error().code == ErrorCode::InvalidName);
}

TEST_CASE("most recent mount shadows earlier ones deterministically") {
    Vfs vfs;
    auto older = std::make_unique<MemoryMount>("older");
    older->add_file("SHARED.DAT", bytes_of("old"));
    older->add_file("ONLY.OLD", bytes_of("old-only"));
    vfs.add_mount(std::move(older));

    auto newer = std::make_unique<MemoryMount>("newer");
    newer->add_file("SHARED.DAT", bytes_of("new"));
    vfs.add_mount(std::move(newer));

    REQUIRE(vfs.mount_count() == 2);
    auto shared = vfs.open("shared.dat");
    REQUIRE(shared.is_ok());
    CHECK(shared.value().origin.rfind("newer:", 0) == 0);
    CHECK(shared.value().size == 3);

    auto only = vfs.open("only.old");
    REQUIRE(only.is_ok());
    CHECK(only.value().origin.rfind("older:", 0) == 0);

    auto diags = vfs.diagnostics();
    REQUIRE(diags.warnings.size() == 1);
    CHECK(diags.warnings[0].find("SHARED.DAT") != std::string::npos);
    CHECK(diags.warnings[0].find("newer") != std::string::npos);
    CHECK(diags.warnings[0].find("older") != std::string::npos);
}

TEST_CASE("within one mount the first registration wins") {
    MemoryMount mount("mem");
    mount.add_file("first.dat", bytes_of("one"));
    mount.add_file("FIRST.DAT", bytes_of("two"));
    // Mount::open takes the already-normalized key.
    auto opened = mount.open("FIRST.DAT");
    REQUIRE(opened.is_ok());
    CHECK(opened.value().bytes == bytes_of("one"));
}

TEST_CASE("missing names report NotFound; keys deduplicate across mounts") {
    Vfs vfs;
    auto mount = std::make_unique<MemoryMount>("mem");
    mount->add_file("A.DAT", bytes_of("a"));
    vfs.add_mount(std::move(mount));

    auto missing = vfs.open("nope.dat");
    REQUIRE_FALSE(missing.is_ok());
    CHECK(missing.error().code == ErrorCode::NotFound);

    auto traversal = vfs.open("../escape");
    REQUIRE_FALSE(traversal.is_ok());
    CHECK(traversal.error().code == ErrorCode::PathTraversal);

    auto second = std::make_unique<MemoryMount>("mem2");
    second->add_file("a.dat", bytes_of("a2"));
    second->add_file("B.DAT", bytes_of("b"));
    vfs.add_mount(std::move(second));

    auto keys = vfs.keys();
    CHECK(keys.size() == 2); // A.DAT present in both mounts counts once
}

TEST_CASE("grp mount reads exact entry bytes without extraction") {
    const auto image =
        fauxbuild::synth::generate_grp({.seed = 11, .file_count = 5, .max_file_size = 128});
    fauxbuild::grp::GrpDiagnostics diags;
    auto mount = GrpMount::from_image("grp:test", image, &diags);
    REQUIRE(mount.is_ok());

    Vfs vfs;
    vfs.add_mount(mount.take());
    for (const auto& key : vfs.keys()) {
        auto file = vfs.open(key);
        REQUIRE(file.is_ok());
        CHECK(file.value().size == file.value().bytes.size());
        CHECK(file.value().origin.rfind("grp:test:SYN", 0) == 0);
    }
}

namespace {
void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}
} // namespace

TEST_CASE("directory-mount collision resolution is deterministic regardless of iteration order") {
    using Table = std::map<std::string, std::string>;
    const std::vector<std::pair<std::string, std::string>> colliding = {{"TILES.ART", "tiles.art"},
                                                                        {"TILES.ART", "TILES.ART"}};

    // Every permutation of the input must resolve identically: the
    // lexicographically first filename wins (M2 review finding — ext4
    // directory iteration order is unspecified/hash-derived, and M7's gate
    // requires Linux/macOS results to match).
    for (auto order :
         {std::vector<std::pair<std::string, std::string>>(colliding),
          std::vector<std::pair<std::string, std::string>>(colliding.rbegin(), colliding.rend())}) {
        const Table table = fauxbuild::DirectoryMount::resolve_file_table(order);
        REQUIRE(table.size() == 1);
        CHECK(table.at("TILES.ART") == "TILES.ART");
    }

    // Distinct keys are unaffected; ties pick the lexicographically first
    // filename ("A.DAT" sorts before "a.dat" in ASCII).
    const Table mixed = fauxbuild::DirectoryMount::resolve_file_table(
        {{"b.dat", "b.dat"}, {"a.dat", "a.dat"}, {"a.dat", "A.DAT"}});
    REQUIRE(mixed.size() == 2);
    CHECK(mixed.at("a.dat") == "A.DAT");
    CHECK(mixed.at("b.dat") == "b.dat");
}

TEST_CASE("directory mount snapshots a flat, case-insensitive view") {
    namespace fs = std::filesystem;
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = fs::temp_directory_path() / ("fauxbuild_vfs_test_" + unique);
    fs::create_directories(root);
    write_file(root / "LOOSE.MAP", bytes_of("map-bytes"));
    write_file(root / "lower.art", bytes_of("art-bytes"));
    write_file(root / ".hidden", bytes_of("skip-me"));

    auto created = DirectoryMount::create(root.string());
    REQUIRE(created.is_ok());

    Vfs vfs;
    vfs.add_mount(created.take());

    auto map = vfs.open("loose.map");
    REQUIRE(map.is_ok());
    CHECK(map.value().bytes == bytes_of("map-bytes"));

    auto art = vfs.open("LOWER.ART");
    REQUIRE(art.is_ok());
    CHECK(art.value().bytes == bytes_of("art-bytes"));

    CHECK_FALSE(vfs.contains(".HIDDEN"));
    CHECK(vfs.keys().size() == 2);

    auto bad_dir = DirectoryMount::create((root / "missing").string());
    REQUIRE_FALSE(bad_dir.is_ok());
    CHECK(bad_dir.error().code == ErrorCode::IoError);

    fs::remove_all(root);
}
