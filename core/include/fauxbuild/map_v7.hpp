#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fauxbuild::mapv7 {

// FAUXBUILD_CLASSIC_V7 world-representation limits (plan §1.2). These are
// profile limits, not format limits: a file declaring more is rejected whole.
inline constexpr int kMaxSectors = 1024;
inline constexpr int kMaxWalls = 8192;
inline constexpr int kMaxSprites = 4096;

// Observed record sizes, locked by black-box verification against a legally
// owned E1L1.MAP (2026-08-23; section sizes sum exactly to the file size):
// sectors 40 bytes, walls 32, sprites 44, sprite count is int16.
inline constexpr std::size_t kSectorRecordSize = 40;
inline constexpr std::size_t kWallRecordSize = 32;
inline constexpr std::size_t kSpriteRecordSize = 44;

// Sentinel for "no reference" in nextwall/nextsector and sprite sectnum.
inline constexpr std::int16_t kNoIndex = -1;

// Stat/cstat bit values from the published MAP v7 format description
// (PROVENANCE row 9), each corroborated against six legally owned maps before
// being adopted — see docs/MILESTONES.md M3 review round 4:
//
//   sector stat 0x0002   P(heinum != 0 | set) = 0.970 vs 0.121 clear (n=4900)
//   wall cstat  0x0010   P(overpicnum != 0 | set) = 0.979 vs 0.029 base (n=15303)
//   sprite cstat 0x0030  takes only 0x0000/0x0010/0x0020; 0x0030 never observed
//                        (n=5355), as a two-bit enum with one reserved value
//
// M3 stores these raw and reports them; nothing interprets them for behaviour
// (slope evaluation is M6, masked rendering M5+).
inline constexpr std::int16_t kStatSloped = 0x0002;
inline constexpr std::int16_t kWallCstatMasked = 0x0010;
inline constexpr std::int16_t kSpriteCstatAlignMask = 0x0030;
inline constexpr std::int16_t kSpriteAlignFace = 0x0000;
inline constexpr std::int16_t kSpriteAlignWall = 0x0010;
inline constexpr std::int16_t kSpriteAlignFloor = 0x0020;

// Texture-placement bits (M6.2B1). Bit positions and meanings are verbatim
// from the same published description (PROVENANCE row 9), re-verified against
// it before adoption; usage counts over the six owned maps are in
// docs/MILESTONES.md (M6.2B1). Floor/ceiling planes:
//
//   floorstat/ceilingstat
//     0x0004  swap x&y
//     0x0008  double smooshiness (UNSUPPORTED — factor not established by
//             approved provenance; deferred with an explicit ledger entry)
//     0x0010  x-flip
//     0x0020  y-flip
//     0x0040  align texture to first wall of sector (relative alignment)
inline constexpr std::int16_t kStatPlaneSwapXY = 0x0004;
inline constexpr std::int16_t kStatPlaneSmoosh = 0x0008;
inline constexpr std::int16_t kStatPlaneFlipX = 0x0010;
inline constexpr std::int16_t kStatPlaneFlipY = 0x0020;
inline constexpr std::int16_t kStatPlaneRelative = 0x0040;
// Wall placement bits (cstat):
//   0x0004  align picture on bottom (0 = top)
//   0x0008  x-flipped
//   0x0100  y-flipped
inline constexpr std::int16_t kWallCstatBottomAligned = 0x0004;
inline constexpr std::int16_t kWallCstatFlipX = 0x0008;
inline constexpr std::int16_t kWallCstatFlipY = 0x0100;

struct StartPose {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int16_t angle = 0; // Build native units, 0..2047
    std::int16_t sector = 0;
};

// Raw MAP v7 sector record. Field-for-field compatible with the file format;
// no interpretation of stats, tags, or shade at parse time.
struct Sector {
    std::int16_t wallptr = 0;
    std::int16_t wallnum = 0;
    std::int32_t ceilingz = 0;
    std::int32_t floorz = 0;
    std::int16_t ceilingstat = 0;
    std::int16_t floorstat = 0;
    std::int16_t ceilingpicnum = 0;
    std::int16_t ceilingheinum = 0;
    std::int8_t ceilingshade = 0;
    std::uint8_t ceilingpal = 0;
    std::uint8_t ceilingxpanning = 0;
    std::uint8_t ceilingypanning = 0;
    std::int16_t floorpicnum = 0;
    std::int16_t floorheinum = 0;
    std::int8_t floorshade = 0;
    std::uint8_t floorpal = 0;
    std::uint8_t floorxpanning = 0;
    std::uint8_t floorypanning = 0;
    std::uint8_t visibility = 0;
    std::uint8_t filler = 0;
    std::int16_t lotag = 0;
    std::int16_t hitag = 0;
    std::int16_t extra = 0;
};

struct Wall {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int16_t point2 = 0;
    std::int16_t nextwall = kNoIndex;
    std::int16_t nextsector = kNoIndex;
    std::int16_t cstat = 0;
    std::int16_t picnum = 0;
    std::int16_t overpicnum = 0;
    std::int8_t shade = 0;
    std::uint8_t pal = 0;
    std::uint8_t xrepeat = 0;
    std::uint8_t yrepeat = 0;
    std::uint8_t xpanning = 0;
    std::uint8_t ypanning = 0;
    std::int16_t lotag = 0;
    std::int16_t hitag = 0;
    std::int16_t extra = 0;
};

struct Sprite {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int16_t cstat = 0;
    std::int16_t picnum = 0;
    std::int8_t shade = 0;
    std::uint8_t pal = 0;
    std::uint8_t clipdist = 0;
    std::uint8_t filler = 0;
    std::uint8_t xrepeat = 0;
    std::uint8_t yrepeat = 0;
    std::int8_t xoffset = 0;
    std::int8_t yoffset = 0;
    std::int16_t sectnum = 0;
    std::int16_t statnum = 0;
    std::int16_t ang = 0;
    std::int16_t owner = 0;
    std::int16_t xvel = 0;
    std::int16_t yvel = 0;
    std::int16_t zvel = 0;
    std::int16_t lotag = 0;
    std::int16_t hitag = 0;
    std::int16_t extra = 0;
};

// Immutable parsed source content (plan §2.4). Raw truth only: derived caches
// (topology, loops, spatial) are built by later milestones from this data and
// never replace it. Record order and IDs are the file's own.
struct MapData {
    StartPose start;
    std::vector<Sector> sectors;
    std::vector<Wall> walls;
    std::vector<Sprite> sprites;
    std::string source;            // origin name for diagnostics
    std::uint64_t source_hash = 0; // FNV-1a64 of the parsed bytes
};

} // namespace fauxbuild::mapv7
