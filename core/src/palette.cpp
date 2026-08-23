#include "fauxbuild/palette.hpp"

#include "fauxbuild/byte_reader.hpp"
#include "fauxbuild/map_io.hpp"

namespace fauxbuild {

Result<PaletteData> read_palette_dat(std::string_view bytes, std::string source) {
    ByteReader reader(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), source);

    auto palette = reader.read_bytes(kPaletteBytes);
    if (!palette.is_ok()) {
        return Result<PaletteData>::err(palette.error());
    }

    auto num_shades = reader.read_i16_le();
    if (!num_shades.is_ok()) {
        return Result<PaletteData>::err(num_shades.error());
    }
    if (num_shades.value() < 0) {
        return Result<PaletteData>::err({source, reader.position() - 2, "palette.header",
                                         ErrorCode::InvalidCount, "negative shade-table count"});
    }

    // Fixed-section arithmetic, validated before allocation:
    //   tables region = size - 770 - 65536, must be a non-negative multiple
    //   of 256, and must cover at least the declared shade tables.
    const auto size = static_cast<std::uint64_t>(bytes.size());
    constexpr std::uint64_t kFixed = kPaletteBytes + 2 + kTranslucencyBytes;
    if (size < kFixed) {
        return Result<PaletteData>::err({source, size, "palette", ErrorCode::OutOfBounds,
                                         "file smaller than fixed sections (768 + 2 + 65536)"});
    }
    const std::uint64_t tables_region = size - kFixed;
    if (tables_region % 256 != 0) {
        return Result<PaletteData>::err({source, size, "palette", ErrorCode::TrailingData,
                                         "table region (" + std::to_string(tables_region) +
                                             " bytes) is not a multiple of 256"});
    }
    const std::uint64_t declared_bytes = static_cast<std::uint64_t>(num_shades.value()) * 256;
    if (declared_bytes > tables_region) {
        return Result<PaletteData>::err(
            {source, reader.position() - 2, "palette.header", ErrorCode::InvalidCount,
             std::to_string(num_shades.value()) + " shade tables (" +
                 std::to_string(declared_bytes) + " bytes) exceed the table region (" +
                 std::to_string(tables_region) + " bytes)"});
    }

    PaletteData data;
    std::copy(palette.value().data, palette.value().data + kPaletteBytes, data.rgb.begin());
    data.num_shades = num_shades.value();

    auto shades = reader.read_bytes(static_cast<std::size_t>(declared_bytes));
    if (!shades.is_ok()) {
        return Result<PaletteData>::err(shades.error());
    }
    data.shade_tables.assign(shades.value().data, shades.value().data + shades.value().size);

    const auto extra_bytes = static_cast<std::size_t>(tables_region - declared_bytes);
    if (extra_bytes > 0) {
        auto extras = reader.read_bytes(extra_bytes);
        if (!extras.is_ok()) {
            return Result<PaletteData>::err(extras.error());
        }
        data.extra_tables.assign(extras.value().data, extras.value().data + extra_bytes);
    }

    auto translucency = reader.read_bytes(kTranslucencyBytes);
    if (!translucency.is_ok()) {
        return Result<PaletteData>::err(translucency.error());
    }
    data.translucency.assign(translucency.value().data,
                             translucency.value().data + kTranslucencyBytes);

    data.source = source;
    data.source_hash = fnv1a64(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return Result<PaletteData>::ok(std::move(data));
}

Result<LookupData> read_lookup_dat(std::string_view bytes, std::string source) {
    ByteReader reader(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), source);

    auto num_swaps = reader.read_u8();
    if (!num_swaps.is_ok()) {
        return Result<LookupData>::err(num_swaps.error());
    }

    // Structure: 1 + n*257 + k*768 must close exactly on the file size.
    const auto size = static_cast<std::uint64_t>(bytes.size());
    const std::uint64_t swaps_bytes =
        static_cast<std::uint64_t>(num_swaps.value()) * kLookupSwapBytes;
    if (size < 1 + swaps_bytes) {
        return Result<LookupData>::err({source, size, "lookup.header", ErrorCode::OutOfBounds,
                                        std::to_string(num_swaps.value()) + " swaps (" +
                                            std::to_string(swaps_bytes) +
                                            " bytes) exceed file size " + std::to_string(size)});
    }
    const std::uint64_t remainder = size - 1 - swaps_bytes;
    if (remainder % kPaletteBytes != 0) {
        return Result<LookupData>::err(
            {source, size, "lookup", ErrorCode::TrailingData,
             "remaining " + std::to_string(remainder) +
                 " bytes are not a multiple of 768 (alternate palettes)"});
    }

    LookupData data;
    data.swaps.reserve(num_swaps.value());
    for (std::uint8_t i = 0; i < num_swaps.value(); ++i) {
        LookupSwap swap;
        auto index = reader.read_u8();
        if (!index.is_ok()) {
            return Result<LookupData>::err(index.error());
        }
        swap.index = index.value();
        auto table = reader.read_bytes(256);
        if (!table.is_ok()) {
            return Result<LookupData>::err(table.error());
        }
        std::copy(table.value().data, table.value().data + 256, swap.table.begin());
        data.swaps.push_back(swap);
    }

    const auto alt_count = static_cast<std::size_t>(remainder / kPaletteBytes);
    data.alt_palettes.reserve(alt_count);
    for (std::size_t i = 0; i < alt_count; ++i) {
        std::array<std::uint8_t, kPaletteBytes> palette{};
        auto bytes_in = reader.read_bytes(kPaletteBytes);
        if (!bytes_in.is_ok()) {
            return Result<LookupData>::err(bytes_in.error());
        }
        std::copy(bytes_in.value().data, bytes_in.value().data + kPaletteBytes, palette.begin());
        data.alt_palettes.push_back(palette);
    }

    data.source = source;
    data.source_hash = fnv1a64(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return Result<LookupData>::ok(std::move(data));
}

namespace {

void put_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

} // namespace

Result<std::vector<std::uint8_t>> write_palette_dat(const PaletteData& data) {
    const std::uint64_t tables =
        static_cast<std::uint64_t>(data.num_shades) * 256 + data.extra_tables.size();
    if (data.num_shades < 0 ||
        data.shade_tables.size() != static_cast<std::size_t>(data.num_shades) * 256) {
        return Result<std::vector<std::uint8_t>>::err(
            {"palette", 0, "palette.write", ErrorCode::InvalidCount,
             "shade-table bytes do not match num_shades"});
    }
    if (data.translucency.size() != kTranslucencyBytes) {
        return Result<std::vector<std::uint8_t>>::err({"palette", 0, "palette.write",
                                                       ErrorCode::InvalidCount,
                                                       "translucency table must be 65536 bytes"});
    }
    if (data.extra_tables.size() % 256 != 0) {
        return Result<std::vector<std::uint8_t>>::err({"palette", 0, "palette.write",
                                                       ErrorCode::InvalidCount,
                                                       "extra tables must be 256-byte units"});
    }

    std::vector<std::uint8_t> out;
    out.reserve(kPaletteBytes + 2 + tables + kTranslucencyBytes);
    out.insert(out.end(), data.rgb.begin(), data.rgb.end());
    put_u16_le(out, static_cast<std::uint16_t>(data.num_shades));
    out.insert(out.end(), data.shade_tables.begin(), data.shade_tables.end());
    out.insert(out.end(), data.extra_tables.begin(), data.extra_tables.end());
    out.insert(out.end(), data.translucency.begin(), data.translucency.end());
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

Result<std::vector<std::uint8_t>> write_lookup_dat(const LookupData& data) {
    std::vector<std::uint8_t> out;
    out.reserve(1 + data.swaps.size() * kLookupSwapBytes +
                data.alt_palettes.size() * kPaletteBytes);
    out.push_back(static_cast<std::uint8_t>(data.swaps.size()));
    for (const auto& swap : data.swaps) {
        out.push_back(swap.index);
        out.insert(out.end(), swap.table.begin(), swap.table.end());
    }
    for (const auto& palette : data.alt_palettes) {
        out.insert(out.end(), palette.begin(), palette.end());
    }
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

} // namespace fauxbuild
