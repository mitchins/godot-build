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

// Files are named SYN0000.DAT, SYN0001.DAT, ... (11 chars; fits the
// 12-byte GRP directory name field with its NUL) with LCG-derived sizes
// (0 included when include_zero_size) and LCG-derived content bytes.
std::vector<std::uint8_t> generate_grp(const GrpSpec& spec);

// Canonical deterministic GRP writer for arbitrary named payloads (M4
// slice 4): 12-byte signature, little-endian file count, directory
// (12-byte NUL-padded names — a full 12-character name fills the field
// with no terminator, matching real containers like TILES000.ART), then
// payloads in the given order. Names are uppercased (real GRPs store
// uppercase; the VFS normalizes queries the same way). Every image this
// writes must round-trip through grp::parse (unit-tested).
struct GrpFileSpec {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

std::vector<std::uint8_t> build_grp(const std::vector<GrpFileSpec>& files);

} // namespace fauxbuild::synth
