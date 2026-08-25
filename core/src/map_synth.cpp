#include "fauxbuild/map_synth.hpp"

#include "fauxbuild/map_io.hpp"

namespace fauxbuild::synth {

mapv7::Wall make_wall(std::int32_t x, std::int32_t y, std::int16_t point2, std::int16_t nextwall,
                      std::int16_t nextsector) {
    mapv7::Wall wall;
    wall.x = x;
    wall.y = y;
    wall.point2 = point2;
    wall.nextwall = nextwall;
    wall.nextsector = nextsector;
    wall.xrepeat = 8;
    wall.yrepeat = 8;
    return wall;
}

mapv7::Sector make_sector(std::int16_t wallptr, std::int16_t wallnum, std::int32_t floorz,
                          std::int32_t ceilingz) {
    mapv7::Sector sector;
    sector.wallptr = wallptr;
    sector.wallnum = wallnum;
    sector.floorz = floorz;
    sector.ceilingz = ceilingz;
    return sector;
}

mapv7::Sprite make_sprite(std::int32_t x, std::int32_t y, std::int32_t z, std::int16_t sectnum,
                          std::int16_t cstat) {
    mapv7::Sprite sprite;
    sprite.x = x;
    sprite.y = y;
    sprite.z = z;
    sprite.sectnum = sectnum;
    sprite.statnum = 0;
    sprite.ang = 0;
    sprite.cstat = cstat;
    sprite.xrepeat = 16;
    sprite.yrepeat = 16;
    sprite.clipdist = 16;
    return sprite;
}

namespace {

constexpr std::int32_t kUnit = 65536; // one Build grid square of wall units

void add_loop(mapv7::MapData& map, const std::int32_t* xs, const std::int32_t* ys,
              std::size_t count) {
    const std::int16_t first = static_cast<std::int16_t>(map.walls.size());
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t next = (i + 1) % count;
        map.walls.push_back(make_wall(xs[i], ys[i], static_cast<std::int16_t>(first + next)));
    }
}

mapv7::MapData minimal_world() {
    mapv7::MapData map;
    const std::int32_t xs[] = {0, kUnit, kUnit, 0};
    const std::int32_t ys[] = {0, 0, kUnit, kUnit};
    add_loop(map, xs, ys, 4);
    map.sectors.push_back(make_sector(0, 4));
    map.start = {kUnit / 2, kUnit / 2, 4096, 512, 0};
    return map;
}

// Metric-scale fixture (D0016 amendment, 2026-08-25). A room whose
// horizontal extent is 1024 Build units and whose floor-to-ceiling delta is
// 16384 Build units. Under the format's 16:1 vertical unit ratio those are
// the SAME physical distance, so the derived shell must be a cube: equal
// render extents on all three axes.
//
// This exists to pin exactly one property and prevent exactly one
// regression -- isotropic axis scaling, which stretches every world into a
// tower. The numbers are generic format quantities; nothing here is derived
// from, or specific to, any game's content.
mapv7::MapData metric_cube_world() {
    mapv7::MapData map;
    const std::int32_t side = 1024;    // horizontal extent, Build X/Y units
    const std::int32_t height = 16384; // vertical extent, Build Z units
    const std::int32_t xs[] = {0, side, side, 0};
    const std::int32_t ys[] = {0, 0, side, side};
    add_loop(map, xs, ys, 4);
    // Build Z points down: the ceiling is the smaller (more negative) value.
    map.sectors.push_back(make_sector(0, 4, 0, -height));
    map.start = {side / 2, side / 2, 0, 0, 0};
    return map;
}

mapv7::MapData square_room_world() {
    mapv7::MapData map = minimal_world();
    auto& sector = map.sectors[0];
    sector.ceilingpicnum = 100; // diagnostic tile index (no Duke meaning)
    sector.floorpicnum = 101;
    sector.visibility = 8;
    sector.ceilingshade = -12;
    sector.floorshade = 8;
    for (auto& wall : map.walls) {
        wall.picnum = 200;
        wall.shade = -8;
        wall.xrepeat = 16;
        wall.yrepeat = 16;
    }
    map.sprites.push_back(make_sprite(kUnit / 2, kUnit / 2, 0, 0));
    map.sprites[0].picnum = 300;
    return map;
}

mapv7::MapData two_sector_portal_world() {
    mapv7::MapData map;
    // Sector A: square (0,0)-(kUnit,kUnit), walls 0..3.
    const std::int32_t ax[] = {0, kUnit, kUnit, 0};
    const std::int32_t ay[] = {0, 0, kUnit, kUnit};
    add_loop(map, ax, ay, 4);
    // Sector B: square (kUnit,0)-(2*kUnit,kUnit), walls 4..7.
    const std::int32_t bx[] = {kUnit, 2 * kUnit, 2 * kUnit, kUnit};
    const std::int32_t by[] = {0, 0, kUnit, kUnit};
    add_loop(map, bx, by, 4);
    // Shared edge x == kUnit: A's wall 1 runs (kUnit,0)->(kUnit,kUnit);
    // B's wall 7 runs (kUnit,kUnit)->(kUnit,0). Reciprocal portal pair.
    map.walls[1].nextwall = 7;
    map.walls[1].nextsector = 1;
    map.walls[7].nextwall = 1;
    map.walls[7].nextsector = 0;

    map.sectors.push_back(make_sector(0, 4));
    map.sectors.push_back(make_sector(4, 4));
    map.start = {kUnit / 2, kUnit / 2, 4096, 512, 0};
    return map;
}

mapv7::MapData two_room_shell(std::int32_t floor_b, std::int32_t ceiling_b) {
    // Shared shape with two_sector_portal_world but with real-content Build
    // vertical ordering (ceilingz < floorz numerically; Build Z grows down)
    // and caller-chosen neighbour heights, so the portal span combinations
    // are testable per case.
    mapv7::MapData map;
    const std::int32_t ax[] = {0, kUnit, kUnit, 0};
    const std::int32_t ay[] = {0, 0, kUnit, kUnit};
    add_loop(map, ax, ay, 4);
    const std::int32_t bx[] = {kUnit, 2 * kUnit, 2 * kUnit, kUnit};
    const std::int32_t by[] = {0, 0, kUnit, kUnit};
    add_loop(map, bx, by, 4);
    map.walls[1].nextwall = 7;
    map.walls[1].nextsector = 1;
    map.walls[7].nextwall = 1;
    map.walls[7].nextsector = 0;
    // A spans [0, 16384] in Build Z (ceiling 0, floor 16384).
    map.sectors.push_back(make_sector(0, 4, 16384, 0));
    map.sectors.push_back(make_sector(4, 4, floor_b, ceiling_b));
    map.start = {kUnit / 2, kUnit / 2, 8192, 512, 0};
    return map;
}

mapv7::MapData portal_heights_world() {
    // B's window [4096, 8192] sits strictly inside A's [0, 16384]: from A the
    // shared wall shows one upper span (0..4096) and one lower span
    // (8192..16384); from B the shared wall shows neither.
    return two_room_shell(8192, 4096);
}

mapv7::MapData portal_step_floor_world() {
    // B has the same ceiling as A but a raised floor at 8192: exactly one
    // lower span from A, nothing from B.
    return two_room_shell(8192, 0);
}

mapv7::MapData asymmetric_probe_world() {
    // M5 slice 2: a rectangular room whose Build x range (1000..11000),
    // y range (2000..7000) and z interval (ceiling 3000, floor 9000) are
    // pairwise distinct at every vertex under the render transform
    // (x, -z, y) * 2^-11, so an accidental axis swap or sign inversion at
    // the Godot boundary cannot survive comparison. Real-content vertical
    // ordering (ceilingz < floorz numerically; Build Z grows down).
    mapv7::MapData map;
    const std::int32_t xs[] = {1000, 11000, 11000, 1000};
    const std::int32_t ys[] = {2000, 2000, 7000, 7000};
    add_loop(map, xs, ys, 4);
    map.sectors.push_back(make_sector(0, 4, 9000, 3000));
    map.start = {6000, 4500, 6000, 512, 0};
    return map;
}

mapv7::MapData non_convex_world() {
    mapv7::MapData map;
    // L-shape: (0,0)->(2u,0)->(2u,u)->(u,u)->(u,2u)->(0,2u)->close.
    const std::int32_t xs[] = {0, 2 * kUnit, 2 * kUnit, kUnit, kUnit, 0};
    const std::int32_t ys[] = {0, 0, kUnit, kUnit, 2 * kUnit, 2 * kUnit};
    add_loop(map, xs, ys, 6);
    map.sectors.push_back(make_sector(0, 6));
    map.start = {kUnit, kUnit / 2, 4096, 512, 0};
    return map;
}

mapv7::MapData multi_loop_world() {
    mapv7::MapData map;
    // Outer square walls 0..3; inner square (hole) walls 4..7, both loops
    // owned by the same sector.
    const std::int32_t ox[] = {0, kUnit, kUnit, 0};
    const std::int32_t oy[] = {0, 0, kUnit, kUnit};
    add_loop(map, ox, oy, 4);
    const std::int32_t ix[] = {kUnit / 4, 3 * kUnit / 4, 3 * kUnit / 4, kUnit / 4};
    const std::int32_t iy[] = {kUnit / 4, kUnit / 4, 3 * kUnit / 4, 3 * kUnit / 4};
    add_loop(map, ix, iy, 4);
    map.sectors.push_back(make_sector(0, 8));
    map.start = {kUnit / 8, kUnit / 8, 4096, 512, 0};
    return map;
}

mapv7::MapData double_hole_world() {
    mapv7::MapData map;
    // One sector, one outer loop, two disjoint holes: exercises multi-hole
    // triangulation and the hole-emptiness invariant.
    const std::int32_t ox[] = {0, 2 * kUnit, 2 * kUnit, 0};
    const std::int32_t oy[] = {0, 0, 2 * kUnit, 2 * kUnit};
    add_loop(map, ox, oy, 4);
    const std::int32_t h1x[] = {kUnit / 4, 3 * kUnit / 4, 3 * kUnit / 4, kUnit / 4};
    const std::int32_t h1y[] = {kUnit / 4, kUnit / 4, 3 * kUnit / 4, 3 * kUnit / 4};
    add_loop(map, h1x, h1y, 4);
    const std::int32_t h2x[] = {5 * kUnit / 4, 7 * kUnit / 4, 7 * kUnit / 4, 5 * kUnit / 4};
    const std::int32_t h2y[] = {5 * kUnit / 4, 5 * kUnit / 4, 7 * kUnit / 4, 7 * kUnit / 4};
    add_loop(map, h2x, h2y, 4);
    map.sectors.push_back(make_sector(0, 12, 16384, 0));
    map.start = {kUnit / 8, kUnit / 8, 4096, 512, 0};
    return map;
}

mapv7::MapData slope_metadata_world() {
    mapv7::MapData map = minimal_world();
    auto& sector = map.sectors[0];
    // A nonzero heinum is only meaningful with the slope bit set; the fixture
    // previously carried the heinum alone, which real content treats as an
    // ignored leftover rather than a slope.
    sector.floorstat |= mapv7::kStatSloped;
    sector.ceilingstat |= mapv7::kStatSloped;
    sector.floorheinum = 1000;
    sector.ceilingheinum = -1000;
    sector.ceilingxpanning = 7;
    sector.ceilingypanning = 3;
    sector.floorxpanning = 5;
    sector.floorypanning = 9;
    sector.ceilingpal = 1;
    sector.floorpal = 2;
    sector.filler = 1;
    return map;
}

// M6 slice 1 slope probes (PROVENANCE STOP 2026-08-25). The approved
// published description establishes heinum as rise/run with 4096 = 45
// degrees and the sector slope bit (stat 0x0002), but NOT which horizontal
// direction the surface tilts along, the sign convention, or the exact
// evaluation equation. These ORIGINAL fixtures are the inputs to the
// black-box Mapster32 experiment that settles axis/sign/anchor; they encode
// no formula and assert no sloped geometry. Until the evaluator exists they
// derive flat at their base Z with the ordinary deferral note.
//
// All probes are one 2x1 rectangular sector (floorz 0, ceilingz 16384) whose
// FIRST wall (wallptr; the published anchor for base heights) runs in a
// controlled direction, so the experiments can separate world-axis tilt from
// first-wall-relative tilt and read the sign:
//   floor_px  first wall along +X, floorheinum  +4096
//   floor_py  first wall along +Y, floorheinum  +4096
//   floor_rx  first wall along -X (px reversed), floorheinum +4096
//   floor_neg first wall along +X, floorheinum  -4096
//   ceiling_px first wall along +X, ceilingheinum +4096
// Shared geometry for every slope probe, so the 2x2 matrix cannot drift on
// anything but first-wall direction and winding.
//
// Small footprint (1024 x 512, a 2:1 rectangle) and a tall room: at
// heinum 4096 the surface tilts steeply enough across 1024 units to be
// unmistakable by eye, and the 65536-unit interval leaves it room to tilt
// without meeting the opposite plane. The experiment is a VISUAL one --
// which way it goes, not how far -- so obviousness beats precision here.
//
// Ordinary (non-inverted) room: Build Z grows downward, so
// ceilingz < startz < floorz, with the eye starting between the planes.
// Ramp fixture geometry (M6.1 slope oracles).
constexpr std::int32_t kRampRun = 1024;
constexpr std::int32_t kRampFloorZ = 32768;
constexpr std::int32_t kRampCeilingZ = -32768;

constexpr std::int32_t kProbeWidth = 1024;
constexpr std::int32_t kProbeHeight = 512;
constexpr std::int32_t kProbeCeilingZ = -32768;
constexpr std::int32_t kProbeStartZ = 0;
constexpr std::int32_t kProbeFloorZ = 32768;

// M6.1 ramp fixtures: the authoritative slope oracles. A right triangle whose
// first wall A->B IS the hinge, with the third corner C exactly 1024 units
// perpendicular to it:
//
//     A = (0,0)   B = (1024,0)   C = (0,1024)
//
// A and B sit on the hinge, so they must hold base Z whatever the heinum. C
// is 1024 units away, so at heinum 4096 (45 degrees) it must differ by
// 1024 * 4096 / 256 = 16384 Build Z units -- equal physical rise and run
// under the 16:1 metric. Every quantity here is derived from the published
// definition, not from the implementation.
mapv7::MapData ramp_world(bool ceiling_slope, std::int16_t heinum) {
    mapv7::MapData map;
    const std::int32_t xs[] = {0, kRampRun, 0};
    const std::int32_t ys[] = {0, 0, kRampRun};
    add_loop(map, xs, ys, 3);
    map.sectors.push_back(make_sector(0, 3, kRampFloorZ, kRampCeilingZ));
    auto& sector = map.sectors[0];
    if (ceiling_slope) {
        sector.ceilingstat |= mapv7::kStatSloped;
        sector.ceilingheinum = heinum;
    } else {
        sector.floorstat |= mapv7::kStatSloped;
        sector.floorheinum = heinum;
    }
    map.start = {kRampRun / 4, kRampRun / 4, 0, 0, 0};
    return map;
}
// Wide-Z ramps: the evaluated Z deliberately leaves the int32 range the MAP
// stores its base Z in, proving the derived value reaches render space without
// a silent narrowing. Hinge A->B along X, apex 200,000,000 units away, so at
// heinum 4096 the delta is 200000000 * 16 = 3,200,000,000 -- beyond INT32_MAX;
// the negative variant goes beyond INT32_MIN.
mapv7::MapData wide_z_ramp_world(std::int16_t heinum) {
    mapv7::MapData map;
    const std::int32_t xs[] = {0, 65536, 0};
    const std::int32_t ys[] = {0, 0, 200000000};
    add_loop(map, xs, ys, 3);
    map.sectors.push_back(make_sector(0, 3, 0, -65536));
    map.sectors[0].floorstat |= mapv7::kStatSloped;
    map.sectors[0].floorheinum = heinum;
    map.start = {1024, 1024, -32768, 0, 0};
    return map;
}
mapv7::MapData slope_wide_z_pos_world() {
    return wide_z_ramp_world(4096);
}
mapv7::MapData slope_wide_z_neg_world() {
    return wide_z_ramp_world(-4096);
}
// A flagged FLOOR whose first-wall hinge has zero length: the hinge, and so
// the floor height, is undefined (D0019). The polygon is a square with a
// duplicated first vertex, so its AREA is unaffected -- otherwise D0018's
// zero-area rule would omit both planes and the test would pass for the wrong
// reason. The CEILING is ordinary and flat, and must survive: it does not
// depend on the undefined plane.
mapv7::MapData slope_degenerate_hinge_world() {
    mapv7::MapData map;
    const std::int32_t side = 1024;
    const std::int32_t xs[] = {0, 0, side, side, 0};
    const std::int32_t ys[] = {0, 0, 0, side, side};
    add_loop(map, xs, ys, 5); // wall 0 -> wall 1 is a zero-length hinge
    map.sectors.push_back(make_sector(0, 5, 32768, -32768));
    map.sectors[0].floorstat |= mapv7::kStatSloped;
    map.sectors[0].floorheinum = 4096;
    // ceilingstat / ceilingheinum stay 0: an ordinary flat ceiling.
    map.start = {side / 2, side / 2, 0, 0, 0};
    return map;
}

mapv7::MapData ramp_floor_pos_world() {
    return ramp_world(false, 4096);
}
mapv7::MapData ramp_floor_neg_world() {
    return ramp_world(false, -4096);
}
mapv7::MapData ramp_ceiling_world() {
    return ramp_world(true, 4096);
}
// A nonzero heinum with the slope flag CLEAR: real content treats this as an
// ignored leftover (M3, n=4,900), so the surface must stay perfectly flat.
mapv7::MapData ramp_stale_heinum_world() {
    mapv7::MapData map = ramp_world(false, 4096);
    map.sectors[0].floorstat = 0; // flag cleared, heinum deliberately left set
    return map;
}

mapv7::MapData slope_probe(const std::int32_t* xs, const std::int32_t* ys, bool ceiling_slope,
                           std::int16_t heinum) {
    mapv7::MapData map;
    add_loop(map, xs, ys, 4);
    // Build Z grows downward, so an ordinary room has ceilingz < floorz --
    // which is what real content consistently shows. The first version of
    // these probes inherited the M3 default interval (floorz 0, ceilingz
    // 16384), an INVERTED room: representable, and fine for the geometry
    // tests that use it, but useless as a behavioural probe for which way a
    // surface tilts. Every probe now uses an ordinary room with the eye
    // between the planes.
    map.sectors.push_back(make_sector(0, 4, kProbeFloorZ, kProbeCeilingZ));
    auto& sector = map.sectors[0];
    // Exactly one surface is flagged; the other keeps stat 0 / heinum 0, so
    // the flagged side is the only variable.
    if (ceiling_slope) {
        sector.ceilingstat |= mapv7::kStatSloped;
        sector.ceilingheinum = heinum;
    } else {
        sector.floorstat |= mapv7::kStatSloped;
        sector.floorheinum = heinum;
    }
    map.start = {kProbeWidth / 2, kProbeHeight / 2, kProbeStartZ, 512, 0};
    return map;
}

mapv7::MapData slope_probe_floor_px_world() {
    const std::int32_t xs[] = {0, kProbeWidth, kProbeWidth, 0};
    const std::int32_t ys[] = {0, 0, kProbeHeight, kProbeHeight};
    return slope_probe(xs, ys, false, 4096);
}

// The slope direction matrix is a true 2x2: first-wall direction (+X/+Y)
// crossed with polygon winding (CCW/CW), same rectangle, same base Z, same
// stat and heinum. Reversing a loop necessarily flips BOTH, so the original
// px/py/rx trio could not attribute an observed difference to one or the
// other. These four can:
//
//                 CCW                     CW
//   first +X      slope_probe_floor_px    slope_probe_floor_px_cw
//   first +Y      slope_probe_floor_py_ccw slope_probe_floor_py
//
// They still encode NO formula. Whether the tilt follows the first wall, a
// world axis, or the surface normal is exactly what the black-box run is
// for.
mapv7::MapData slope_probe_floor_px_cw_world() {
    // First wall (0,U)->(2U,U): +X. Loop runs clockwise.
    const std::int32_t xs[] = {0, kProbeWidth, kProbeWidth, 0};
    const std::int32_t ys[] = {kProbeHeight, kProbeHeight, 0, 0};
    return slope_probe(xs, ys, false, 4096);
}
mapv7::MapData slope_probe_floor_py_ccw_world() {
    // First wall (2U,0)->(2U,U): +Y. Loop runs counter-clockwise.
    const std::int32_t xs[] = {kProbeWidth, kProbeWidth, 0, 0};
    const std::int32_t ys[] = {0, kProbeHeight, kProbeHeight, 0};
    return slope_probe(xs, ys, false, 4096);
}
mapv7::MapData slope_probe_floor_py_world() {
    const std::int32_t xs[] = {0, 0, kProbeWidth, kProbeWidth};
    const std::int32_t ys[] = {0, kProbeHeight, kProbeHeight, 0};
    return slope_probe(xs, ys, false, 4096);
}

mapv7::MapData slope_probe_floor_rx_world() {
    const std::int32_t xs[] = {kProbeWidth, 0, 0, kProbeWidth};
    const std::int32_t ys[] = {0, 0, kProbeHeight, kProbeHeight};
    return slope_probe(xs, ys, false, 4096);
}

mapv7::MapData slope_probe_floor_neg_world() {
    const std::int32_t xs[] = {0, kProbeWidth, kProbeWidth, 0};
    const std::int32_t ys[] = {0, 0, kProbeHeight, kProbeHeight};
    return slope_probe(xs, ys, false, -4096);
}

mapv7::MapData slope_probe_ceiling_px_world() {
    const std::int32_t xs[] = {0, kProbeWidth, kProbeWidth, 0};
    const std::int32_t ys[] = {0, 0, kProbeHeight, kProbeHeight};
    return slope_probe(xs, ys, true, 4096);
}

mapv7::MapData masked_wall_world() {
    mapv7::MapData map = two_sector_portal_world();
    // Masking flag on both sides of the shared edge. 0x0002 was used here
    // previously; it is not the masking bit, and real masked walls are
    // identified by 0x0010 travelling with a nonzero overpicnum.
    map.walls[1].cstat = mapv7::kWallCstatMasked;
    map.walls[1].overpicnum = 210;
    map.walls[7].cstat = mapv7::kWallCstatMasked;
    map.walls[7].overpicnum = 210;
    return map;
}

mapv7::MapData sprite_orientations_world() {
    mapv7::MapData map = square_room_world();
    const std::int32_t half = kUnit / 2;
    // Orientation is the two-bit field 0x0030, not independent flags: the old
    // 0x0008/0x0010 pair could set "wall" and "floor" at once, which real
    // content never does.
    map.sprites.push_back(make_sprite(half - 8192, half, 0, 0, mapv7::kSpriteAlignWall));
    map.sprites.push_back(make_sprite(half, half - 8192, 0, 0, mapv7::kSpriteAlignFloor));
    map.sprites.push_back(make_sprite(half + 8192, half, 0, 0, mapv7::kSpriteAlignFace));
    map.sprites[1].ang = 768;
    map.sprites[2].statnum = 1;
    return map;
}

mapv7::MapData max_reasonable_counts_world() {
    mapv7::MapData map;
    // 16 x 4 grid of independent square rooms: 64 sectors, 256 walls,
    // 128 sprites — comfortably large while far under profile limits.
    for (std::int32_t gy = 0; gy < 4; ++gy) {
        for (std::int32_t gx = 0; gx < 16; ++gx) {
            const std::int32_t x0 = gx * 2 * kUnit;
            const std::int32_t y0 = gy * 2 * kUnit;
            const std::int32_t xs[] = {x0, x0 + kUnit, x0 + kUnit, x0};
            const std::int32_t ys[] = {y0, y0, y0 + kUnit, y0 + kUnit};
            add_loop(map, xs, ys, 4);
            const auto sector_index = static_cast<std::int16_t>(map.sectors.size());
            map.sectors.push_back(make_sector(static_cast<std::int16_t>(map.walls.size() - 4), 4));
            map.sprites.push_back(make_sprite(x0 + kUnit / 4, y0 + kUnit / 4, 0, sector_index));
            map.sprites.push_back(
                make_sprite(x0 + 3 * kUnit / 4, y0 + 3 * kUnit / 4, 0, sector_index));
        }
    }
    map.start = {kUnit / 2, kUnit / 2, 4096, 512, 0};
    return map;
}

} // namespace

std::vector<std::string> map_fixture_names() {
    return {
        "minimal",
        "square_room",
        "two_sector_portal",
        "non_convex",
        "multi_loop",
        "double_hole",
        "portal_heights",
        "portal_step_floor",
        "slope_metadata",
        "masked_wall",
        "sprite_orientations",
        "max_reasonable_counts",
        "asymmetric_probe",
        "metric_cube",
        "slope_probe_floor_px",
        "slope_probe_floor_px_cw",
        "slope_probe_floor_py",
        "slope_probe_floor_py_ccw",
        "slope_probe_floor_rx",
        "slope_probe_floor_neg",
        "slope_probe_ceiling_px",
    };
}

Result<mapv7::MapData> map_fixture(const std::string& name) {
    mapv7::MapData map;
    if (name == "minimal") {
        map = minimal_world();
    } else if (name == "slope_wide_z_pos") {
        map = slope_wide_z_pos_world();
    } else if (name == "slope_wide_z_neg") {
        map = slope_wide_z_neg_world();
    } else if (name == "slope_degenerate_hinge") {
        map = slope_degenerate_hinge_world();
    } else if (name == "ramp_floor_pos") {
        map = ramp_floor_pos_world();
    } else if (name == "ramp_floor_neg") {
        map = ramp_floor_neg_world();
    } else if (name == "ramp_ceiling") {
        map = ramp_ceiling_world();
    } else if (name == "ramp_stale_heinum") {
        map = ramp_stale_heinum_world();
    } else if (name == "metric_cube") {
        map = metric_cube_world();
    } else if (name == "square_room") {
        map = square_room_world();
    } else if (name == "two_sector_portal") {
        map = two_sector_portal_world();
    } else if (name == "non_convex") {
        map = non_convex_world();
    } else if (name == "multi_loop") {
        map = multi_loop_world();
    } else if (name == "double_hole") {
        map = double_hole_world();
    } else if (name == "portal_heights") {
        map = portal_heights_world();
    } else if (name == "portal_step_floor") {
        map = portal_step_floor_world();
    } else if (name == "slope_metadata") {
        map = slope_metadata_world();
    } else if (name == "masked_wall") {
        map = masked_wall_world();
    } else if (name == "sprite_orientations") {
        map = sprite_orientations_world();
    } else if (name == "max_reasonable_counts") {
        map = max_reasonable_counts_world();
    } else if (name == "asymmetric_probe") {
        map = asymmetric_probe_world();
    } else if (name == "slope_probe_floor_px") {
        map = slope_probe_floor_px_world();
    } else if (name == "slope_probe_floor_px_cw") {
        map = slope_probe_floor_px_cw_world();
    } else if (name == "slope_probe_floor_py_ccw") {
        map = slope_probe_floor_py_ccw_world();
    } else if (name == "slope_probe_floor_py") {
        map = slope_probe_floor_py_world();
    } else if (name == "slope_probe_floor_rx") {
        map = slope_probe_floor_rx_world();
    } else if (name == "slope_probe_floor_neg") {
        map = slope_probe_floor_neg_world();
    } else if (name == "slope_probe_ceiling_px") {
        map = slope_probe_ceiling_px_world();
    } else {
        return Result<mapv7::MapData>::err(
            {"synth", 0, "fixture", ErrorCode::InvalidName, "unknown fixture " + name});
    }
    map.source = "synthetic:" + name;
    return Result<mapv7::MapData>::ok(std::move(map));
}

Result<std::vector<std::uint8_t>> serialize_map_fixture(const std::string& name) {
    auto map = map_fixture(name);
    if (!map.is_ok()) {
        return Result<std::vector<std::uint8_t>>::err(map.error());
    }
    return write_map(map.value());
}

} // namespace fauxbuild::synth
