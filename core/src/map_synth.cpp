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
        "minimal",        "square_room", "two_sector_portal",   "non_convex",
        "multi_loop",     "double_hole", "portal_heights",      "portal_step_floor",
        "slope_metadata", "masked_wall", "sprite_orientations", "max_reasonable_counts",
    };
}

Result<mapv7::MapData> map_fixture(const std::string& name) {
    mapv7::MapData map;
    if (name == "minimal") {
        map = minimal_world();
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
