#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "fauxbuild/map_v7.hpp"

namespace fauxbuild {

// Semantic comparison of two parsed maps (task §8: diff parsed fields, not
// hashes). Reports every differing field as "<record>.<field>: <a> != <b>".
struct MapDiff {
    bool identical = true;
    std::vector<std::string> notes;
};

MapDiff diff_maps(const mapv7::MapData& a, const mapv7::MapData& b, std::size_t max_notes = 64);

} // namespace fauxbuild
