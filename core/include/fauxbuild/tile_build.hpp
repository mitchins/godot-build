#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fauxbuild/art.hpp"
#include "fauxbuild/palette.hpp"
#include "fauxbuild/result.hpp"
#include "fauxbuild/tile_manifest.hpp"

namespace fauxbuild {

// Original synthetic source formats for the M4 fixture compilers (plan §7.5).
// Text DSLs, deterministic, hand-editable; no image dependencies (PNG import
// belongs to the future game art pipeline, not the engine repo). All content
// produced from them is original — no proprietary bytes can enter through
// this path.
//
// Tileset DSL (fixtures/source/tiles/*.tileset):
//   tileset <name>
//   tile <name> <w> <h> pattern=checker|solid|ramp|grid|indexed [params]
//   anim <name> <w> <h> frames=<n> type=forward|oscillating|backward
//        speed=<n> pattern=<same generators> [params]
//   pivot <name> <xc> <yc>
// Pattern params: checker a=<i> b=<i> square=<n>; solid color=<i>;
// ramp from=<i> to=<i>; grid major=<n> line=<i> bg=<i>; indexed (pixel=i).
struct TilesetTile {
    std::string name;
    std::int16_t width = 0;
    std::int16_t height = 0;
    std::int8_t x_center = 0;
    std::int8_t y_center = 0;
    std::uint8_t anim_type = 0;
    std::uint8_t frames = 1;
    std::uint8_t speed = 0;
    std::string pattern;
    std::vector<long> params; // positional meaning per pattern
};

struct TilesetDef {
    std::string name;
    std::vector<TilesetTile> tiles;
};

Result<TilesetDef> parse_tileset(std::string_view text, std::string source);

// Builds ART tiles from the tileset, assigning picnums through the manifest
// (existing names keep their picnums; new names append as max+1; a tile that
// disappeared from the manifest is an error — stability is enforced here).
// One output tile per manifest entry; animation sets expand to `frames`
// consecutive tiles sharing picanm.
struct BuiltArt {
    ArtData art;
    TileManifest manifest; // updated (possibly with new assignments)
};

// Names whose art the caller has explicitly accepted as changed (D0014 rule 4).
// A picnum's *number* is immutable; its artwork is not — redrawing a tile while
// keeping its picnum is ordinary authoring. Unacknowledged changes still fail
// closed, so the manifest stays a drift detector without freezing the art.
// An entry may name a tile ("hero") or a single animation frame ("hero#2").
struct TileUpdateAcceptance {
    std::vector<std::string> accepted_names;
    bool accepts(const std::string& entry_name) const;
};

Result<BuiltArt> build_art_from_tileset(const TilesetDef& tileset, const TileManifest& manifest,
                                        const TileUpdateAcceptance& accepted = {});

// Palette spec DSL (fixtures/source/palettes/*.palette):
//   palette <name>
//   entry <index> <r> <g> <b>          (components 0..63)
//   ramp <start> <count> <r g b> -> <r g b>
//   swap <palette-index> tint <r g b>  (generates a lookup swap)
//   shades <count>                     (default 32)
//   translucent <on|off>               (default on)
struct PaletteSpec {
    struct Ramp {
        std::int32_t start = 0;
        std::int32_t count = 0;
        std::int32_t from[3] = {0, 0, 0};
        std::int32_t to[3] = {0, 0, 0};
    };
    struct Swap {
        std::uint8_t index = 0;
        std::int32_t tint[3] = {0, 0, 0};
    };
    std::string name;
    std::int32_t entry[256][3] = {};
    bool entry_set[256] = {};
    std::vector<Ramp> ramps;
    std::vector<Swap> swaps;
    std::int32_t shades = 32;
    bool translucent = true;
};

Result<PaletteSpec> parse_palette_spec(std::string_view text, std::string source);

// Generates PALETTE.DAT/LOOKUP.DAT content from the spec. Deterministic and
// original: shade tables map each index to the index of closest luminance at
// each darkness level; the translucency table maps pairs to the nearest
// palette entry by averaged RGB distance (ties resolved by lowest index).
Result<PaletteData> build_palette_dat(const PaletteSpec& spec);
Result<LookupData> build_lookup_dat(const PaletteSpec& spec);

} // namespace fauxbuild
