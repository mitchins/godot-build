#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fauxbuild/result.hpp"

namespace fauxbuild {

// Tile manifest: the picnum authority for original FauxBuild content
// (plan §7.5, M4 slice 3). A picnum's meaning must never change when tiles
// are added: entries are immutable once assigned, new tiles append as
// max_picnum + 1, and removal/rename is an explicit error. Stability is a
// tested property (tests/unit/tile_manifest.test.cpp), not a comment.
//
// Text format (deterministic, diff-friendly):
//   # fauxbuild tile manifest v1
//   # picnum  name  w  h  xc  yc  anim  frames  speed
//   0  checker_a  64 64  0 0  none 0 0
struct TileManifestEntry {
    std::int32_t picnum = 0;
    std::string name;
    std::int16_t width = 0;
    std::int16_t height = 0;
    std::int8_t x_center = 0;
    std::int8_t y_center = 0;
    std::uint8_t anim_type = 0; // 0 none, 1 oscillating, 2 forward, 3 backward
    std::uint8_t frames = 0;
    std::uint8_t speed = 0;
};

struct TileManifest {
    std::vector<TileManifestEntry> entries; // sorted by picnum

    const TileManifestEntry* find(const std::string& name) const;
    const TileManifestEntry* find_picnum(std::int32_t picnum) const;

    // Appends a new entry with picnum = max+1 (0 if empty). Fails if the
    // name already exists — existing assignments are immutable.
    Result<std::int32_t> assign(const std::string& name, std::int16_t width, std::int16_t height,
                                std::int8_t x_center, std::int8_t y_center, std::uint8_t anim_type,
                                std::uint8_t frames, std::uint8_t speed);
};

Result<TileManifest> parse_tile_manifest(std::string_view text, std::string source);
Result<std::string> write_tile_manifest(const TileManifest& manifest);

// Validates manifest invariants: picnums strictly ascending starting at 0
// with no gaps, names unique, dims non-negative.
Result<void> validate_tile_manifest(const TileManifest& manifest);

} // namespace fauxbuild
