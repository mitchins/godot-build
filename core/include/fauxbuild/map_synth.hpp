#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fauxbuild/map_v7.hpp"
#include "fauxbuild/result.hpp"

namespace fauxbuild::synth {

// Field-value helpers for building original synthetic MAP v7 worlds.
// Tile numbers used by fixtures are FauxBuild diagnostic tiles — they carry
// no Duke meaning (plan §7.1, AGENTS.md).
mapv7::Wall make_wall(std::int32_t x, std::int32_t y, std::int16_t point2,
                      std::int16_t nextwall = mapv7::kNoIndex,
                      std::int16_t nextsector = mapv7::kNoIndex);
mapv7::Sector make_sector(std::int16_t wallptr, std::int16_t wallnum, std::int32_t floorz = 0,
                          std::int32_t ceilingz = 16384);
mapv7::Sprite make_sprite(std::int32_t x, std::int32_t y, std::int32_t z, std::int16_t sectnum,
                          std::int16_t cstat = 0);

// Named deterministic fixture worlds (task §9). Same name -> identical bytes.
std::vector<std::string> map_fixture_names();
Result<mapv7::MapData> map_fixture(const std::string& name);
Result<std::vector<std::uint8_t>> serialize_map_fixture(const std::string& name);

} // namespace fauxbuild::synth
