#include "fauxbuild/map_diff.hpp"

#include <algorithm>

namespace fauxbuild {

namespace {

struct Collector {
    MapDiff& diff;
    std::size_t max_notes;

    void note(const std::string& record, const std::string& field, auto a, auto b) {
        if (a == b) {
            return;
        }
        diff.identical = false;
        if (diff.notes.size() >= max_notes) {
            return;
        }
        diff.notes.push_back(record + "." + field + ": " + std::to_string(a) +
                             " != " + std::to_string(b));
    }
};

} // namespace

MapDiff diff_maps(const mapv7::MapData& a, const mapv7::MapData& b, std::size_t max_notes) {
    MapDiff diff;
    Collector collector{diff, max_notes};

    collector.note("start", "x", a.start.x, b.start.x);
    collector.note("start", "y", a.start.y, b.start.y);
    collector.note("start", "z", a.start.z, b.start.z);
    collector.note("start", "angle", a.start.angle, b.start.angle);
    collector.note("start", "sector", a.start.sector, b.start.sector);

    collector.note("counts", "sectors", a.sectors.size(), b.sectors.size());
    collector.note("counts", "walls", a.walls.size(), b.walls.size());
    collector.note("counts", "sprites", a.sprites.size(), b.sprites.size());

    const std::size_t common_sectors = std::min(a.sectors.size(), b.sectors.size());
    for (std::size_t i = 0; i < common_sectors; ++i) {
        const std::string record = "sector[" + std::to_string(i) + "]";
        const auto& x = a.sectors[i];
        const auto& y = b.sectors[i];
        collector.note(record, "wallptr", x.wallptr, y.wallptr);
        collector.note(record, "wallnum", x.wallnum, y.wallnum);
        collector.note(record, "ceilingz", x.ceilingz, y.ceilingz);
        collector.note(record, "floorz", x.floorz, y.floorz);
        collector.note(record, "ceilingstat", x.ceilingstat, y.ceilingstat);
        collector.note(record, "floorstat", x.floorstat, y.floorstat);
        collector.note(record, "ceilingpicnum", x.ceilingpicnum, y.ceilingpicnum);
        collector.note(record, "ceilingheinum", x.ceilingheinum, y.ceilingheinum);
        collector.note(record, "ceilingshade", x.ceilingshade, y.ceilingshade);
        collector.note(record, "ceilingpal", x.ceilingpal, y.ceilingpal);
        collector.note(record, "ceilingxpanning", x.ceilingxpanning, y.ceilingxpanning);
        collector.note(record, "ceilingypanning", x.ceilingypanning, y.ceilingypanning);
        collector.note(record, "floorpicnum", x.floorpicnum, y.floorpicnum);
        collector.note(record, "floorheinum", x.floorheinum, y.floorheinum);
        collector.note(record, "floorshade", x.floorshade, y.floorshade);
        collector.note(record, "floorpal", x.floorpal, y.floorpal);
        collector.note(record, "floorxpanning", x.floorxpanning, y.floorxpanning);
        collector.note(record, "floorypanning", x.floorypanning, y.floorypanning);
        collector.note(record, "visibility", x.visibility, y.visibility);
        collector.note(record, "filler", x.filler, y.filler);
        collector.note(record, "lotag", x.lotag, y.lotag);
        collector.note(record, "hitag", x.hitag, y.hitag);
        collector.note(record, "extra", x.extra, y.extra);
    }

    const std::size_t common_walls = std::min(a.walls.size(), b.walls.size());
    for (std::size_t i = 0; i < common_walls; ++i) {
        const std::string record = "wall[" + std::to_string(i) + "]";
        const auto& x = a.walls[i];
        const auto& y = b.walls[i];
        collector.note(record, "x", x.x, y.x);
        collector.note(record, "y", x.y, y.y);
        collector.note(record, "point2", x.point2, y.point2);
        collector.note(record, "nextwall", x.nextwall, y.nextwall);
        collector.note(record, "nextsector", x.nextsector, y.nextsector);
        collector.note(record, "cstat", x.cstat, y.cstat);
        collector.note(record, "picnum", x.picnum, y.picnum);
        collector.note(record, "overpicnum", x.overpicnum, y.overpicnum);
        collector.note(record, "shade", x.shade, y.shade);
        collector.note(record, "pal", x.pal, y.pal);
        collector.note(record, "xrepeat", x.xrepeat, y.xrepeat);
        collector.note(record, "yrepeat", x.yrepeat, y.yrepeat);
        collector.note(record, "xpanning", x.xpanning, y.xpanning);
        collector.note(record, "ypanning", x.ypanning, y.ypanning);
        collector.note(record, "lotag", x.lotag, y.lotag);
        collector.note(record, "hitag", x.hitag, y.hitag);
        collector.note(record, "extra", x.extra, y.extra);
    }

    const std::size_t common_sprites = std::min(a.sprites.size(), b.sprites.size());
    for (std::size_t i = 0; i < common_sprites; ++i) {
        const std::string record = "sprite[" + std::to_string(i) + "]";
        const auto& x = a.sprites[i];
        const auto& y = b.sprites[i];
        collector.note(record, "x", x.x, y.x);
        collector.note(record, "y", x.y, y.y);
        collector.note(record, "z", x.z, y.z);
        collector.note(record, "cstat", x.cstat, y.cstat);
        collector.note(record, "picnum", x.picnum, y.picnum);
        collector.note(record, "shade", x.shade, y.shade);
        collector.note(record, "pal", x.pal, y.pal);
        collector.note(record, "clipdist", x.clipdist, y.clipdist);
        collector.note(record, "filler", x.filler, y.filler);
        collector.note(record, "xrepeat", x.xrepeat, y.xrepeat);
        collector.note(record, "yrepeat", x.yrepeat, y.yrepeat);
        collector.note(record, "xoffset", x.xoffset, y.xoffset);
        collector.note(record, "yoffset", x.yoffset, y.yoffset);
        collector.note(record, "sectnum", x.sectnum, y.sectnum);
        collector.note(record, "statnum", x.statnum, y.statnum);
        collector.note(record, "ang", x.ang, y.ang);
        collector.note(record, "owner", x.owner, y.owner);
        collector.note(record, "xvel", x.xvel, y.xvel);
        collector.note(record, "yvel", x.yvel, y.yvel);
        collector.note(record, "zvel", x.zvel, y.zvel);
        collector.note(record, "lotag", x.lotag, y.lotag);
        collector.note(record, "hitag", x.hitag, y.hitag);
        collector.note(record, "extra", x.extra, y.extra);
    }

    return diff;
}

} // namespace fauxbuild
