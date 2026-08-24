#pragma once

namespace fauxbuild::tool {

// fbtool inspect-atlas --grp FILE | --dir DIR [--page-width N] [--page-height N]
// Human-gate A command (M4 slice 4): discover TILES*.ART + PALETTE.DAT +
// LOOKUP.DAT through the VFS (mounted GRP or loose directory), compose the
// global picnum namespace, pack the indexed atlas, and print generic
// statistics. Output policy: counts and dimensions only — no pixel data,
// no content hashes, nothing proprietary.
int inspect_atlas(int argc, char** argv);

} // namespace fauxbuild::tool
