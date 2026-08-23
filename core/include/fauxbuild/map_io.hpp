#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "fauxbuild/map_v7.hpp"
#include "fauxbuild/result.hpp"

namespace fauxbuild {

// Reads a MAP v7 image. Byte-level parse only: counts, limits, widths, EOF,
// version, trailing data. Structural/referential validation is a separate
// stage (map_validate.hpp) — parse success says the bytes decoded, not that
// the world is coherent. Never partially initializes a MapData.
Result<mapv7::MapData> read_map(std::string_view bytes, std::string source);

// Canonical serialization (task §7): the current logical map as a
// deterministic, valid MAP v7 byte stream. Counts outside the classic profile
// limits are rejected. Reading the output back must produce a semantic diff
// of zero (round-trip contract, tested).
Result<std::vector<std::uint8_t>> write_map(const mapv7::MapData& map);

// FNV-1a 64 over the given bytes; used for source hashes and diff shortcuts.
std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size);

} // namespace fauxbuild
