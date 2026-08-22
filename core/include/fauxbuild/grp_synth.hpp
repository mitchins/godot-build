#pragma once

#include <cstdint>
#include <vector>

namespace fauxbuild::synth {

// Deterministic synthetic GRP generator (M2). Same spec -> identical bytes,
// so fixtures are reproducible and CI never depends on randomness.
struct GrpSpec {
    std::uint32_t seed = 1;
    std::uint32_t file_count = 8;
    std::uint32_t max_file_size = 256;
    bool include_zero_size = true;
};

// Files are named SYNTH0000.DAT, SYNTH0001.DAT, ... with LCG-derived sizes
// (0 included when include_zero_size) and LCG-derived content bytes.
std::vector<std::uint8_t> generate_grp(const GrpSpec& spec);

} // namespace fauxbuild::synth
