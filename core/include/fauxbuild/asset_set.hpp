#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fauxbuild/art.hpp"
#include "fauxbuild/palette.hpp"
#include "fauxbuild/result.hpp"
#include "fauxbuild/vfs.hpp"

namespace fauxbuild {

// The game asset bundle (M4 slice 4): the TILES*.ART set, PALETTE.DAT, and
// LOOKUP.DAT, discovered and read through ONE Vfs — a loose directory, a
// mounted GRP, or a stack of both. Discovery works on normalized VFS names;
// the ART ordering comes from the declared tile ranges, never from
// filenames. With this, "give me picnum N" (atlas lookup) does not know or
// care whether the assets came from loose files, one ART file, or thirteen
// inside a GRP — the slice's acceptance abstraction.

struct AssetSet {
    PaletteData palette;
    LookupData lookup;
    std::vector<ArtData> arts;          // ascending localtilestart (ties: name)
    std::vector<std::string> art_names; // normalized VFS names, same order
};

// Normalized-name predicate: "TILES" prefix + ".ART" suffix (TILES000.ART,
// TILES001.ART, ...). Names are already VFS-normalized (uppercase, flat).
bool is_art_name(std::string_view name);

// Loads and parses the full set. Fails closed with NotFound when there is
// no ART file, no PALETTE.DAT, or no LOOKUP.DAT in any mount; parse errors
// propagate with the originating mount/origin string.
Result<AssetSet> load_asset_set(const Vfs& vfs);

} // namespace fauxbuild
