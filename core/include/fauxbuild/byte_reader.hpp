#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "fauxbuild/result.hpp"

namespace fauxbuild {

// Simple non-owning byte view; avoids the non-standard
// char_traits<unsigned char> specialization of basic_string_view.
struct ByteSpan {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// Bounds-checked little-endian reader over untrusted bytes (plan §6.3).
// The only sanctioned way to decode binary content: never reinterpret_cast
// file bytes into packed structs. All failures are structured errors.
class ByteReader {
  public:
    ByteReader(const std::uint8_t* data, std::size_t size, std::string source);

    std::uint64_t position() const { return pos_; }
    std::uint64_t remaining() const { return size_ - pos_; }
    std::uint64_t size() const { return size_; }
    const std::string& source() const { return source_; }

    Result<std::uint8_t> read_u8();
    Result<std::int16_t> read_i16_le();
    Result<std::uint16_t> read_u16_le();
    Result<std::int32_t> read_i32_le();
    Result<std::uint32_t> read_u32_le();
    Result<ByteSpan> read_bytes(std::size_t count);
    Result<void> skip(std::size_t count);

  private:
    std::uint8_t read_u8_unchecked();
    void read_u32_unchecked(std::uint8_t out[4]);

    const std::uint8_t* data_;
    std::uint64_t size_;
    std::uint64_t pos_ = 0;
    std::string source_;
};

} // namespace fauxbuild
