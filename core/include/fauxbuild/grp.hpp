#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fauxbuild/result.hpp"

namespace fauxbuild::grp {

// GRP container (published binary format): 12-byte "KenSilverman" signature,
// uint32 file count, then count x 16-byte directory entries (12-byte NUL-padded
// name, uint32 size), then concatenated file data in directory order. The header
// is 16 bytes; there is no declared data-length field. Offsets are implicit and
// cumulative from the end of the directory.

struct GrpEntry {
    std::string name; // original case as stored in the directory
    std::string key;  // normalized lookup key (ASCII uppercase)
    std::uint32_t size = 0;
    std::uint64_t offset = 0; // absolute offset within the container
};

struct GrpData {
    std::uint32_t file_count = 0;
    std::uint64_t data_start = 0; // 16 + 16 * file_count
    std::vector<GrpEntry> entries;
};

// Parser resource limit, not a format limit: the GRP format states no maximum
// file count, but the directory is 16 bytes per entry while a parsed GrpEntry is
// 64, so an unbounded count lets untrusted input amplify 4x into process memory
// (a 1 GiB container would materialize ~4 GiB of entries). Real archives are in
// the low thousands of files; this is ~40x the largest we expect. Raising it is
// a decision record, not a code change (D0011).
inline constexpr std::uint32_t kMaxEntryCount = 65536;

struct GrpDiagnostics {
    std::vector<std::string> warnings;
};

// Parses a complete GRP image. Validation: signature, file count within
// kMaxEntryCount, directory fits in the image, every entry's name is a legal
// flat name, every entry's data range fits in the image. Tolerated oddities become warnings via
// `diags` (duplicate names: first entry wins; declared data length != actual sum). Never partially
// initializes: the caller gets complete data or an error.
Result<GrpData> parse(std::string_view image, std::string source, GrpDiagnostics* diags = nullptr);

} // namespace fauxbuild::grp
