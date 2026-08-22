#include <doctest/doctest.h>

#include "fauxbuild/byte_reader.hpp"

using fauxbuild::ByteReader;
using fauxbuild::ErrorCode;
using fauxbuild::Result;

namespace {

const std::uint8_t kSample[] = {0x01, 0x02, 0x03, 0x04, 0x80, 0xFF, 0x00, 0x7F};

} // namespace

TEST_CASE("little-endian decodes exact values") {
    ByteReader reader(kSample, sizeof(kSample), "sample");
    auto a = reader.read_u8();
    REQUIRE(a.is_ok());
    CHECK(a.value() == 0x01);

    auto b = reader.read_u16_le();
    REQUIRE(b.is_ok());
    CHECK(b.value() == 0x0302);

    auto c = reader.read_i16_le();
    REQUIRE(c.is_ok());
    CHECK(c.value() == static_cast<std::int16_t>(0x8004));

    auto rest = reader.read_bytes(3);
    REQUIRE(rest.is_ok());
    CHECK(rest.value().size == 3);
    CHECK(rest.value().data[0] == 0xFF);
    CHECK(rest.value().data[1] == 0x00);
    CHECK(rest.value().data[2] == 0x7F);

    CHECK(reader.remaining() == 0);
}

TEST_CASE("negative int decoding") {
    const std::uint8_t bytes[] = {0xFF, 0xFF, 0xFF, 0xFF};
    ByteReader reader(bytes, sizeof(bytes), "neg");
    auto i32 = reader.read_i32_le();
    REQUIRE(i32.is_ok());
    CHECK(i32.value() == -1);
}

TEST_CASE("truncation reports source, offset, record, code") {
    ByteReader reader(kSample, 2, "tiny");
    REQUIRE(reader.read_u8().is_ok()); // offset 0
    auto wide = reader.read_u32_le();  // needs 4, has 1
    REQUIRE_FALSE(wide.is_ok());
    const auto& error = wide.error();
    CHECK(error.source == "tiny");
    CHECK(error.offset == 1);
    CHECK(error.code == ErrorCode::Truncated);
    CHECK_FALSE(error.record.empty());
    CHECK_FALSE(error.detail.empty());

    auto rest = reader.read_bytes(4); // still only 1 byte left
    REQUIRE_FALSE(rest.is_ok());
    CHECK(rest.error().offset == 1);
}

TEST_CASE("read_bytes returns exact spans and advances") {
    ByteReader reader(kSample, sizeof(kSample), "sample");
    auto span = reader.read_bytes(4);
    REQUIRE(span.is_ok());
    CHECK(span.value().size == 4);
    CHECK(span.value().data[0] == 0x01);
    CHECK(span.value().data[3] == 0x04);
    CHECK(reader.position() == 4);

    auto all = reader.read_bytes(4);
    REQUIRE(all.is_ok());
    CHECK(reader.position() == 8);
    CHECK(reader.remaining() == 0);

    auto past = reader.read_bytes(1);
    REQUIRE_FALSE(past.is_ok());
}

TEST_CASE("skip is bounds-checked too") {
    ByteReader reader(kSample, sizeof(kSample), "sample");
    CHECK(reader.skip(8).is_ok());
    CHECK(reader.position() == 8);
    auto beyond = reader.skip(1);
    REQUIRE_FALSE(beyond.is_ok());
    CHECK(beyond.error().code == ErrorCode::Truncated);
}

TEST_CASE("empty input rejects the first read") {
    ByteReader reader(nullptr, 0, "empty");
    auto byte = reader.read_u8();
    REQUIRE_FALSE(byte.is_ok());
    CHECK(byte.error().offset == 0);
    CHECK(byte.error().source == "empty");
}

TEST_CASE("huge counts cannot wrap the bounds check") {
    // Regression for the M2-review overflow: pos_ + count wrapped for counts
    // near SIZE_MAX and produced a bogus OK span (or rewound the position).
    ByteReader reader(kSample, sizeof(kSample), "wrap");
    REQUIRE(reader.skip(4).is_ok());

    auto huge_span = reader.read_bytes(SIZE_MAX - 15);
    REQUIRE_FALSE(huge_span.is_ok());
    CHECK(huge_span.error().code == ErrorCode::Truncated);
    CHECK(huge_span.error().offset == 4);

    auto huge_skip = reader.skip(SIZE_MAX - 15);
    REQUIRE_FALSE(huge_skip.is_ok());
    CHECK(huge_skip.error().code == ErrorCode::Truncated);

    // State is untouched by the rejected calls; normal reads still work.
    CHECK(reader.position() == 4);
    auto rest = reader.read_bytes(4);
    REQUIRE(rest.is_ok());
    CHECK(rest.value().size == 4);
}

TEST_CASE("empty reader yields a null span without forming a pointer") {
    fauxbuild::ByteReader reader(nullptr, 0, "empty");
    auto span = reader.read_bytes(0);
    REQUIRE(span.is_ok());
    CHECK(span.value().data == nullptr);
    CHECK(span.value().size == 0);
    CHECK(reader.position() == 0);
}
