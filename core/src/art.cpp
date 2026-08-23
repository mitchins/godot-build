#include "fauxbuild/art.hpp"

#include "fauxbuild/byte_reader.hpp"
#include "fauxbuild/map_io.hpp"

namespace fauxbuild {

namespace {

PicanmBits decode_picanm(std::uint32_t raw) {
    PicanmBits bits;
    bits.frames = raw & 0x3F;
    bits.anim_type = (raw >> 6) & 0x3;
    const auto xc = static_cast<std::uint8_t>((raw >> 8) & 0xFF);
    const auto yc = static_cast<std::uint8_t>((raw >> 16) & 0xFF);
    bits.x_center = static_cast<std::int8_t>(xc);
    bits.y_center = static_cast<std::int8_t>(yc);
    bits.speed = (raw >> 24) & 0xF;
    bits.raw = raw;
    return bits;
}

void put_i32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(raw >> (8 * i)));
    }
}

void put_i16(std::vector<std::uint8_t>& out, std::int16_t value) {
    const auto raw = static_cast<std::uint16_t>(value);
    out.push_back(static_cast<std::uint8_t>(raw));
    out.push_back(static_cast<std::uint8_t>(raw >> 8));
}

} // namespace

Result<ArtData> read_art(std::string_view bytes, std::string source) {
    ByteReader reader(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), source);

    auto version = reader.read_i32_le();
    if (!version.is_ok()) {
        return Result<ArtData>::err(version.error());
    }
    if (version.value() != 1) {
        return Result<ArtData>::err(
            {source, 0, "art.header", ErrorCode::UnsupportedVersion,
             "version " + std::to_string(version.value()) + " is not ART v1 (1)"});
    }

    ArtData data;
    data.version = version.value();

    auto numtiles = reader.read_i32_le();
    if (!numtiles.is_ok()) {
        return Result<ArtData>::err(numtiles.error());
    }
    data.numtiles_field = numtiles.value(); // global count; not used for sizing

    auto start = reader.read_i32_le();
    if (!start.is_ok()) {
        return Result<ArtData>::err(start.error());
    }
    auto end = reader.read_i32_le();
    if (!end.is_ok()) {
        return Result<ArtData>::err(end.error());
    }
    data.localtilestart = start.value();
    data.localtileend = end.value();

    if (start.value() < 0 || end.value() < start.value()) {
        return Result<ArtData>::err({source, 12, "art.header", ErrorCode::InvalidCount,
                                     "tile range [" + std::to_string(start.value()) + ", " +
                                         std::to_string(end.value()) +
                                         "] is not a valid ascending range"});
    }
    const auto count =
        static_cast<std::uint64_t>(end.value()) - static_cast<std::uint64_t>(start.value()) + 1;
    // Validate the fixed header+dims region fits before any allocation.
    if (16 + count * 8 > reader.size()) {
        return Result<ArtData>::err({source, reader.size(), "art.header", ErrorCode::OutOfBounds,
                                     "dimensions/picanm arrays for " + std::to_string(count) +
                                         " tiles need 16 + " + std::to_string(count * 8) +
                                         " bytes but file has " + std::to_string(reader.size())});
    }

    std::vector<std::int16_t> widths, heights;
    std::vector<std::uint32_t> picanm;
    widths.reserve(static_cast<std::size_t>(count));
    heights.reserve(static_cast<std::size_t>(count));
    picanm.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        auto w = reader.read_i16_le();
        if (!w.is_ok()) {
            return Result<ArtData>::err(w.error());
        }
        widths.push_back(w.value());
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        auto h = reader.read_i16_le();
        if (!h.is_ok()) {
            return Result<ArtData>::err(h.error());
        }
        heights.push_back(h.value());
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        auto raw = reader.read_u32_le();
        if (!raw.is_ok()) {
            return Result<ArtData>::err(raw.error());
        }
        picanm.push_back(raw.value());
    }

    for (std::uint64_t i = 0; i < count; ++i) {
        if (widths[static_cast<std::size_t>(i)] < 0 || heights[static_cast<std::size_t>(i)] < 0) {
            return Result<ArtData>::err({source, 16, "art.dimensions", ErrorCode::InvalidCount,
                                         "tile " + std::to_string(i) + " has negative dimensions"});
        }
    }

    data.tiles.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        ArtTile tile;
        tile.width = widths[static_cast<std::size_t>(i)];
        tile.height = heights[static_cast<std::size_t>(i)];
        tile.meta = decode_picanm(picanm[static_cast<std::size_t>(i)]);
        const auto size =
            static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height);
        if (size > 0) {
            auto pixels = reader.read_bytes(size);
            if (!pixels.is_ok()) {
                return Result<ArtData>::err(pixels.error());
            }
            tile.pixels.assign(pixels.value().data, pixels.value().data + size);
        }
        data.tiles.push_back(std::move(tile));
    }

    if (reader.remaining() != 0) {
        return Result<ArtData>::err(
            {source, reader.position(), "art.trailer", ErrorCode::TrailingData,
             std::to_string(reader.remaining()) + " trailing bytes after tile data"});
    }

    data.source = source;
    data.source_hash = fnv1a64(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return Result<ArtData>::ok(std::move(data));
}

Result<std::vector<std::uint8_t>> write_art(const ArtData& data) {
    const auto count = data.tiles.size();
    const auto declared = static_cast<std::uint64_t>(data.localtileend) -
                          static_cast<std::uint64_t>(data.localtilestart) + 1;
    if (data.localtilestart < 0 || data.localtileend < data.localtilestart) {
        return Result<std::vector<std::uint8_t>>::err(
            {"art", 0, "art.write", ErrorCode::InvalidCount, "invalid tile range"});
    }
    if (declared != count) {
        return Result<std::vector<std::uint8_t>>::err(
            {"art", 0, "art.write", ErrorCode::InvalidCount,
             "tile range implies " + std::to_string(declared) + " tiles but data holds " +
                 std::to_string(count)});
    }

    std::uint64_t area = 0;
    for (const auto& tile : data.tiles) {
        if (tile.width < 0 || tile.height < 0 ||
            tile.pixels.size() !=
                static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height)) {
            return Result<std::vector<std::uint8_t>>::err(
                {"art", 0, "art.write", ErrorCode::InvalidCount,
                 "tile pixel count does not match dimensions"});
        }
        area += static_cast<std::uint64_t>(tile.width) * tile.height;
    }

    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(16 + count * 8 + area));
    put_i32(out, data.version);
    put_i32(out, data.numtiles_field);
    put_i32(out, data.localtilestart);
    put_i32(out, data.localtileend);
    for (const auto& tile : data.tiles) {
        put_i16(out, tile.width);
    }
    for (const auto& tile : data.tiles) {
        put_i16(out, tile.height);
    }
    for (const auto& tile : data.tiles) {
        put_i32(out, static_cast<std::int32_t>(tile.meta.raw));
    }
    for (const auto& tile : data.tiles) {
        out.insert(out.end(), tile.pixels.begin(), tile.pixels.end());
    }
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

} // namespace fauxbuild
