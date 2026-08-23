#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fauxbuild/result.hpp"

namespace fauxbuild {

// Palette/lookup formats (published description: ModdingWiki "Duke Nukem 3D
// Palette Format", PROVENANCE row 11; every adopted fact corroborated against
// a legally owned PALETTE.DAT/LOOKUP.DAT before encoding — see
// COMPATIBILITY_SCOPE rows 2–3 and D0013).
//
// Components are stored exactly as in the file: 6-bit VGA values (0..63).
// Conversion to any display depth belongs to a future presentation boundary,
// never to this data (task brief: no RGBA assumption in core/).

inline constexpr std::size_t kPaletteBytes = 768;        // 256 x 3
inline constexpr std::size_t kTranslucencyBytes = 65536; // 256 x 256
inline constexpr std::size_t kLookupSwapBytes = 257;     // index + 256 entries

struct PaletteData {
    std::array<std::uint8_t, kPaletteBytes> rgb{};
    std::int16_t num_shades = 0;            // declared shade-table count (palette-0 ramp)
    std::vector<std::uint8_t> shade_tables; // num_shades * 256
    // Tables beyond the declared count (observed: 32 in real PALETTE.DAT,
    // undeclared in-band, implied by total size; preserved verbatim — D0013).
    std::vector<std::uint8_t> extra_tables;
    std::vector<std::uint8_t> translucency; // 65536

    std::string source;
    std::uint64_t source_hash = 0;
};

struct LookupSwap {
    std::uint8_t index = 0; // palette index this swap defines
    std::array<std::uint8_t, 256> table{};
};

struct LookupData {
    std::vector<LookupSwap> swaps;
    std::vector<std::array<std::uint8_t, kPaletteBytes>> alt_palettes;

    std::string source;
    std::uint64_t source_hash = 0;
};

// Byte-level parse, bounded and structured: counts are validated against the
// actual size before any allocation; the fixed-section arithmetic must close
// exactly or the input fails closed. Never partially initializes.
Result<PaletteData> read_palette_dat(std::string_view bytes, std::string source);
Result<LookupData> read_lookup_dat(std::string_view bytes, std::string source);

// Canonical serialization. Blobs are emitted verbatim, so parsing real
// content and writing it back is byte-identical (tested, fuzzed).
Result<std::vector<std::uint8_t>> write_palette_dat(const PaletteData& data);
Result<std::vector<std::uint8_t>> write_lookup_dat(const LookupData& data);

} // namespace fauxbuild
