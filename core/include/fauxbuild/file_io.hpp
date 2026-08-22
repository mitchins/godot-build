#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fauxbuild/result.hpp"

namespace fauxbuild {

// Small file IO helpers used by mounts and tools. Bounded reads: content
// larger than max_bytes fails with TooLarge instead of allocating (plan §19:
// parser memory is bounded by validated counts and file sizes).
Result<std::vector<std::uint8_t>> read_file_bytes(const std::string& path,
                                                  std::uint64_t max_bytes = 1ull << 30);
Result<std::size_t> write_file_bytes(const std::string& path, const std::uint8_t* data,
                                     std::size_t size);

} // namespace fauxbuild
