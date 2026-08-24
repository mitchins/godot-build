#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fauxbuild/result.hpp"

namespace fauxbuild {

// ART tile container (published description: ModdingWiki "ART Format
// (Build)", PROVENANCE row 10). Every structural claim was corroborated
// against three legally owned TILES###.ART files before encoding
// (COMPATIBILITY_SCOPE row 0d): version==1; the numtiles header field has no
// established meaning — the published description calls it unused, and the
// only thing observation establishes is that it is not the per-file count —
// the count is end-start+1; ranges chain contiguously; and
// 16 + n*8 + sum(w*h) closes exactly on real file sizes.
//
// Pixel ordering is column-major per the published description; size
// arithmetic cannot distinguish orderings, so pixels are preserved VERBATIM
// in file order with no conversion (the brief's "no silent transpose" rule,
// taken to its safe conclusion: no transpose at all). Interpretation of
// ordering belongs to the future presentation boundary.

struct PicanmBits {
    std::uint8_t frames = 0;    // bits 5-0
    std::uint8_t anim_type = 0; // bits 7-6: 0 none, 1 oscillating, 2 forward, 3 backward
    std::int8_t x_center = 0;   // bits 15-8, signed
    std::int8_t y_center = 0;   // bits 23-16, signed
    std::uint8_t speed = 0;     // bits 27-24
    std::uint32_t raw = 0;      // verbatim dword
};

struct ArtTile {
    std::int16_t width = 0;
    std::int16_t height = 0;
    PicanmBits meta;
    std::vector<std::uint8_t> pixels; // file order, width*height bytes
};

struct ArtData {
    std::int32_t version = 1;
    std::int32_t numtiles_field = 0; // preserved raw; NOT the per-file count
    std::int32_t localtilestart = 0;
    std::int32_t localtileend = 0;
    std::vector<ArtTile> tiles;

    std::string source;
    std::uint64_t source_hash = 0;
};

// Byte-level parse: version/range validated before allocation; dims arrays
// must fit; per-tile pixel reads are bounds-checked and the file must close
// exactly (no trailing bytes). Never partially initializes.
Result<ArtData> read_art(std::string_view bytes, std::string source);

// Canonical serialization; verbatim blobs, so parse->write is byte-identical
// for anything that parses (unit-tested, fuzz-enforced).
Result<std::vector<std::uint8_t>> write_art(const ArtData& data);

} // namespace fauxbuild
