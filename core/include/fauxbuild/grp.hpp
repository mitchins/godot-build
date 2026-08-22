#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fauxbuild/result.hpp"

namespace fauxbuild::grp {

// GRP container (published binary format): 12-byte "KenSilverman" signature,
// uint32 file count, uint32 declared data length, then count x 16-byte
// directory entries (12-byte NUL-padded name, uint32 size), then concatenated
// file data in directory order. Offsets are implicit and cumulative.

struct GrpEntry {
    std::string name; // original case as stored in the directory
    std::string key;  // normalized lookup key (ASCII uppercase)
    std::uint32_t size = 0;
    std::uint64_t offset = 0; // absolute offset within the container
};

struct GrpData {
    std::uint32_t file_count = 0;
    std::uint32_t data_length = 0; // header-declared data area size
    std::uint64_t data_start = 0;  // 16 + 16 * file_count
    std::vector<GrpEntry> entries;
};

struct GrpDiagnostics {
    std::vector<std::string> warnings;
};

// Parses a complete GRP image. Validation: signature, directory fits in the
// image, every entry's name is a legal flat name, every entry's data range
// fits in the image. Tolerated oddities become warnings via `diags`
// (duplicate names: first entry wins; declared data length != actual sum).
// Never partially initializes: the caller gets complete data or an error.
Result<GrpData> parse(std::string_view image, std::string source, GrpDiagnostics* diags = nullptr);

} // namespace fauxbuild::grp
