#include "fauxbuild/byte_reader.hpp"

namespace fauxbuild {

ByteReader::ByteReader(const std::uint8_t* data, std::size_t size, std::string source)
    : data_(data), size_(size), source_(std::move(source)) {}

std::uint8_t ByteReader::read_u8_unchecked() {
    return data_[pos_++];
}

void ByteReader::read_u32_unchecked(std::uint8_t out[4]) {
    for (int i = 0; i < 4; ++i) {
        out[i] = data_[pos_ + static_cast<std::uint64_t>(i)];
    }
    pos_ += 4;
}

Result<std::uint8_t> ByteReader::read_u8() {
    if (pos_ + 1 > size_) {
        return Result<std::uint8_t>::err(
            {source_, pos_, "byte", ErrorCode::Truncated, "need 1 byte, 0 remaining"});
    }
    return Result<std::uint8_t>::ok(read_u8_unchecked());
}

Result<std::int16_t> ByteReader::read_i16_le() {
    auto lo = read_u8();
    if (!lo.is_ok()) {
        return Result<std::int16_t>::err(lo.error());
    }
    auto hi = read_u8();
    if (!hi.is_ok()) {
        return Result<std::int16_t>::err(hi.error());
    }
    const std::uint16_t raw =
        static_cast<std::uint16_t>(lo.value()) | (static_cast<std::uint16_t>(hi.value()) << 8);
    return Result<std::int16_t>::ok(static_cast<std::int16_t>(raw));
}

Result<std::uint16_t> ByteReader::read_u16_le() {
    auto lo = read_u8();
    if (!lo.is_ok()) {
        return Result<std::uint16_t>::err(lo.error());
    }
    auto hi = read_u8();
    if (!hi.is_ok()) {
        return Result<std::uint16_t>::err(hi.error());
    }
    return Result<std::uint16_t>::ok(static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(lo.value()) | (static_cast<std::uint16_t>(hi.value()) << 8)));
}

Result<std::int32_t> ByteReader::read_i32_le() {
    const auto raw = read_u32_le();
    if (!raw.is_ok()) {
        return Result<std::int32_t>::err(raw.error());
    }
    return Result<std::int32_t>::ok(static_cast<std::int32_t>(raw.value()));
}

Result<std::uint32_t> ByteReader::read_u32_le() {
    if (pos_ + 4 > size_) {
        return Result<std::uint32_t>::err(
            {source_, pos_, "u32", ErrorCode::Truncated,
             "need 4 bytes, " + std::to_string(size_ - pos_) + " remaining"});
    }
    std::uint8_t raw[4];
    read_u32_unchecked(raw);
    return Result<std::uint32_t>::ok(
        static_cast<std::uint32_t>(raw[0]) | (static_cast<std::uint32_t>(raw[1]) << 8) |
        (static_cast<std::uint32_t>(raw[2]) << 16) | (static_cast<std::uint32_t>(raw[3]) << 24));
}

Result<ByteSpan> ByteReader::read_bytes(std::size_t count) {
    const auto wide = static_cast<std::uint64_t>(count);
    if (pos_ + wide > size_) {
        return Result<ByteSpan>::err({source_, pos_, "bytes", ErrorCode::Truncated,
                                      "need " + std::to_string(count) + " bytes, " +
                                          std::to_string(size_ - pos_) + " remaining"});
    }
    const ByteSpan span(data_ + pos_, count);
    pos_ += wide;
    return Result<ByteSpan>::ok(span);
}

Result<void> ByteReader::skip(std::size_t count) {
    const auto wide = static_cast<std::uint64_t>(count);
    if (pos_ + wide > size_) {
        return Result<void>::err({source_, pos_, "skip", ErrorCode::Truncated,
                                  "need " + std::to_string(count) + " bytes, " +
                                      std::to_string(size_ - pos_) + " remaining"});
    }
    pos_ += wide;
    return Result<void>::ok();
}

} // namespace fauxbuild
