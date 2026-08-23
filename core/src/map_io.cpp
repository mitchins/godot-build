#include "fauxbuild/map_io.hpp"

#include "fauxbuild/byte_reader.hpp"

namespace fauxbuild {

namespace {

void put_i16(std::vector<std::uint8_t>& out, std::int16_t value) {
    const auto raw = static_cast<std::uint16_t>(value);
    out.push_back(static_cast<std::uint8_t>(raw));
    out.push_back(static_cast<std::uint8_t>(raw >> 8));
}

void put_i32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(raw >> (8 * i)));
    }
}

std::int8_t as_i8(std::uint8_t value) {
    return static_cast<std::int8_t>(value);
}

template <typename Target, typename Source> Result<Target> propagate(const Result<Source>& failed) {
    return Result<Target>::err(failed.error());
}

// Sector file order: wallptr wallnum ceilingz floorz ceilingstat floorstat
// ceilingpicnum ceilingheinum ceilingshade ceilingpal ceilingxpanning
// ceilingypanning floorpicnum floorheinum floorshade floorpal floorxpanning
// floorypanning visibility filler lotag hitag extra  (40 bytes)
Result<mapv7::Sector> read_sector(ByteReader& reader) {
    mapv7::Sector s;
    for (auto* field : {&s.wallptr, &s.wallnum}) {
        auto value = reader.read_i16_le();
        if (!value.is_ok())
            return propagate<mapv7::Sector>(value);
        *field = value.value();
    }
    for (auto* field : {&s.ceilingz, &s.floorz}) {
        auto value = reader.read_i32_le();
        if (!value.is_ok())
            return propagate<mapv7::Sector>(value);
        *field = value.value();
    }
    for (auto* field : {&s.ceilingstat, &s.floorstat, &s.ceilingpicnum, &s.ceilingheinum}) {
        auto value = reader.read_i16_le();
        if (!value.is_ok())
            return propagate<mapv7::Sector>(value);
        *field = value.value();
    }
    {
        auto value = reader.read_u8();
        if (!value.is_ok())
            return propagate<mapv7::Sector>(value);
        s.ceilingshade = as_i8(value.value());
    }
    for (auto* field : {&s.ceilingpal, &s.ceilingxpanning, &s.ceilingypanning}) {
        auto value = reader.read_u8();
        if (!value.is_ok())
            return propagate<mapv7::Sector>(value);
        *field = value.value();
    }
    for (auto* field : {&s.floorpicnum, &s.floorheinum}) {
        auto value = reader.read_i16_le();
        if (!value.is_ok())
            return propagate<mapv7::Sector>(value);
        *field = value.value();
    }
    {
        auto value = reader.read_u8();
        if (!value.is_ok())
            return propagate<mapv7::Sector>(value);
        s.floorshade = as_i8(value.value());
    }
    for (auto* field :
         {&s.floorpal, &s.floorxpanning, &s.floorypanning, &s.visibility, &s.filler}) {
        auto value = reader.read_u8();
        if (!value.is_ok())
            return propagate<mapv7::Sector>(value);
        *field = value.value();
    }
    for (auto* field : {&s.lotag, &s.hitag, &s.extra}) {
        auto value = reader.read_i16_le();
        if (!value.is_ok())
            return propagate<mapv7::Sector>(value);
        *field = value.value();
    }
    return Result<mapv7::Sector>::ok(s);
}

// Wall file order: x y point2 nextwall nextsector cstat picnum overpicnum
// shade pal xrepeat yrepeat xpanning ypanning lotag hitag extra (32 bytes)
Result<mapv7::Wall> read_wall(ByteReader& reader) {
    mapv7::Wall w;
    for (auto* field : {&w.x, &w.y}) {
        auto value = reader.read_i32_le();
        if (!value.is_ok())
            return propagate<mapv7::Wall>(value);
        *field = value.value();
    }
    for (auto* field :
         {&w.point2, &w.nextwall, &w.nextsector, &w.cstat, &w.picnum, &w.overpicnum}) {
        auto value = reader.read_i16_le();
        if (!value.is_ok())
            return propagate<mapv7::Wall>(value);
        *field = value.value();
    }
    {
        auto value = reader.read_u8();
        if (!value.is_ok())
            return propagate<mapv7::Wall>(value);
        w.shade = as_i8(value.value());
    }
    for (auto* field : {&w.pal, &w.xrepeat, &w.yrepeat, &w.xpanning, &w.ypanning}) {
        auto value = reader.read_u8();
        if (!value.is_ok())
            return propagate<mapv7::Wall>(value);
        *field = value.value();
    }
    for (auto* field : {&w.lotag, &w.hitag, &w.extra}) {
        auto value = reader.read_i16_le();
        if (!value.is_ok())
            return propagate<mapv7::Wall>(value);
        *field = value.value();
    }
    return Result<mapv7::Wall>::ok(w);
}

// Sprite file order: x y z cstat picnum shade pal clipdist filler xrepeat
// yrepeat xoffset yoffset sectnum statnum ang owner xvel yvel zvel lotag
// hitag extra (44 bytes)
Result<mapv7::Sprite> read_sprite(ByteReader& reader) {
    mapv7::Sprite sp;
    for (auto* field : {&sp.x, &sp.y, &sp.z}) {
        auto value = reader.read_i32_le();
        if (!value.is_ok())
            return propagate<mapv7::Sprite>(value);
        *field = value.value();
    }
    for (auto* field : {&sp.cstat, &sp.picnum}) {
        auto value = reader.read_i16_le();
        if (!value.is_ok())
            return propagate<mapv7::Sprite>(value);
        *field = value.value();
    }
    {
        auto value = reader.read_u8();
        if (!value.is_ok())
            return propagate<mapv7::Sprite>(value);
        sp.shade = as_i8(value.value());
    }
    for (auto* field : {&sp.pal, &sp.clipdist, &sp.filler, &sp.xrepeat, &sp.yrepeat}) {
        auto value = reader.read_u8();
        if (!value.is_ok())
            return propagate<mapv7::Sprite>(value);
        *field = value.value();
    }
    for (auto* field : {&sp.xoffset, &sp.yoffset}) {
        auto value = reader.read_u8();
        if (!value.is_ok())
            return propagate<mapv7::Sprite>(value);
        *field = as_i8(value.value());
    }
    for (auto* field : {&sp.sectnum, &sp.statnum, &sp.ang, &sp.owner, &sp.xvel, &sp.yvel, &sp.zvel,
                        &sp.lotag, &sp.hitag, &sp.extra}) {
        auto value = reader.read_i16_le();
        if (!value.is_ok())
            return propagate<mapv7::Sprite>(value);
        *field = value.value();
    }
    return Result<mapv7::Sprite>::ok(sp);
}

} // namespace

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size) {
    // FNV-1a 64-bit offset basis (0xcbf29ce484222325). A previous value here was
    // this constant with two digits dropped: self-consistent, so nothing broke,
    // but not FNV-1a. Known-answer tests now pin it (map_reader.test.cpp).
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

Result<mapv7::MapData> read_map(std::string_view bytes, std::string source) {
    ByteReader reader(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), source);

    auto version = reader.read_i32_le();
    if (!version.is_ok()) {
        return propagate<mapv7::MapData>(version);
    }
    if (version.value() != 7) {
        return Result<mapv7::MapData>::err(
            {source, 0, "map.header", ErrorCode::UnsupportedVersion,
             "version " + std::to_string(version.value()) + " is not MAP v7 (7)"});
    }

    mapv7::MapData map;
    map.source = source;

    for (auto* field : {&map.start.x, &map.start.y, &map.start.z}) {
        auto value = reader.read_i32_le();
        if (!value.is_ok())
            return propagate<mapv7::MapData>(value);
        *field = value.value();
    }
    for (auto* field : {&map.start.angle, &map.start.sector}) {
        auto value = reader.read_i16_le();
        if (!value.is_ok())
            return propagate<mapv7::MapData>(value);
        *field = value.value();
    }

    auto check_count = [&](const Result<std::int16_t>& count, ErrorCode negative_code,
                           ErrorCode limit_code, const char* what, int limit) -> Result<void> {
        if (!count.is_ok()) {
            return propagate<void>(count);
        }
        if (count.value() < 0) {
            return Result<void>::err({source, reader.position() - 2, "map.header", negative_code,
                                      std::string("negative ") + what + " count"});
        }
        if (count.value() > limit) {
            return Result<void>::err({source, reader.position() - 2, "map.header", limit_code,
                                      std::to_string(count.value()) + " " + what +
                                          "s exceeds classic profile limit " +
                                          std::to_string(limit)});
        }
        return Result<void>::ok();
    };

    auto numsectors = reader.read_i16_le();
    auto sector_count = check_count(numsectors, ErrorCode::InvalidCount, ErrorCode::TooManySectors,
                                    "sector", mapv7::kMaxSectors);
    if (!sector_count.is_ok())
        return propagate<mapv7::MapData>(sector_count);
    map.sectors.reserve(static_cast<std::size_t>(numsectors.value()));
    for (std::int16_t i = 0; i < numsectors.value(); ++i) {
        auto record = read_sector(reader);
        if (!record.is_ok())
            return propagate<mapv7::MapData>(record);
        map.sectors.push_back(record.take());
    }

    auto numwalls = reader.read_i16_le();
    auto wall_count = check_count(numwalls, ErrorCode::InvalidCount, ErrorCode::TooManyWalls,
                                  "wall", mapv7::kMaxWalls);
    if (!wall_count.is_ok())
        return propagate<mapv7::MapData>(wall_count);
    map.walls.reserve(static_cast<std::size_t>(numwalls.value()));
    for (std::int16_t i = 0; i < numwalls.value(); ++i) {
        auto record = read_wall(reader);
        if (!record.is_ok())
            return propagate<mapv7::MapData>(record);
        map.walls.push_back(record.take());
    }

    auto numsprites = reader.read_i16_le();
    auto sprite_count = check_count(numsprites, ErrorCode::InvalidCount, ErrorCode::TooManySprites,
                                    "sprite", mapv7::kMaxSprites);
    if (!sprite_count.is_ok())
        return propagate<mapv7::MapData>(sprite_count);
    map.sprites.reserve(static_cast<std::size_t>(numsprites.value()));
    for (std::int16_t i = 0; i < numsprites.value(); ++i) {
        auto record = read_sprite(reader);
        if (!record.is_ok())
            return propagate<mapv7::MapData>(record);
        map.sprites.push_back(record.take());
    }

    if (reader.remaining() != 0) {
        return Result<mapv7::MapData>::err(
            {source, reader.position(), "map.trailer", ErrorCode::TrailingData,
             std::to_string(reader.remaining()) + " trailing bytes after sprite table"});
    }

    map.source_hash = fnv1a64(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return Result<mapv7::MapData>::ok(std::move(map));
}

Result<std::vector<std::uint8_t>> write_map(const mapv7::MapData& map) {
    if (map.sectors.size() > static_cast<std::size_t>(mapv7::kMaxSectors)) {
        return Result<std::vector<std::uint8_t>>::err(
            {"map", 0, "map.header", ErrorCode::TooManySectors,
             std::to_string(map.sectors.size()) + " sectors exceeds classic profile limit " +
                 std::to_string(mapv7::kMaxSectors)});
    }
    if (map.walls.size() > static_cast<std::size_t>(mapv7::kMaxWalls)) {
        return Result<std::vector<std::uint8_t>>::err(
            {"map", 0, "map.header", ErrorCode::TooManyWalls,
             std::to_string(map.walls.size()) + " walls exceeds classic profile limit " +
                 std::to_string(mapv7::kMaxWalls)});
    }
    if (map.sprites.size() > static_cast<std::size_t>(mapv7::kMaxSprites)) {
        return Result<std::vector<std::uint8_t>>::err(
            {"map", 0, "map.header", ErrorCode::TooManySprites,
             std::to_string(map.sprites.size()) + " sprites exceeds classic profile limit " +
                 std::to_string(mapv7::kMaxSprites)});
    }

    std::vector<std::uint8_t> out;
    out.reserve(20 + 2 + map.sectors.size() * mapv7::kSectorRecordSize + 2 +
                map.walls.size() * mapv7::kWallRecordSize + 2 +
                map.sprites.size() * mapv7::kSpriteRecordSize);

    put_i32(out, 7);
    put_i32(out, map.start.x);
    put_i32(out, map.start.y);
    put_i32(out, map.start.z);
    put_i16(out, map.start.angle);
    put_i16(out, map.start.sector);

    put_i16(out, static_cast<std::int16_t>(map.sectors.size()));
    for (const auto& s : map.sectors) {
        put_i16(out, s.wallptr);
        put_i16(out, s.wallnum);
        put_i32(out, s.ceilingz);
        put_i32(out, s.floorz);
        put_i16(out, s.ceilingstat);
        put_i16(out, s.floorstat);
        put_i16(out, s.ceilingpicnum);
        put_i16(out, s.ceilingheinum);
        out.push_back(static_cast<std::uint8_t>(s.ceilingshade));
        out.push_back(s.ceilingpal);
        out.push_back(s.ceilingxpanning);
        out.push_back(s.ceilingypanning);
        put_i16(out, s.floorpicnum);
        put_i16(out, s.floorheinum);
        out.push_back(static_cast<std::uint8_t>(s.floorshade));
        out.push_back(s.floorpal);
        out.push_back(s.floorxpanning);
        out.push_back(s.floorypanning);
        out.push_back(s.visibility);
        out.push_back(s.filler);
        put_i16(out, s.lotag);
        put_i16(out, s.hitag);
        put_i16(out, s.extra);
    }

    put_i16(out, static_cast<std::int16_t>(map.walls.size()));
    for (const auto& w : map.walls) {
        put_i32(out, w.x);
        put_i32(out, w.y);
        put_i16(out, w.point2);
        put_i16(out, w.nextwall);
        put_i16(out, w.nextsector);
        put_i16(out, w.cstat);
        put_i16(out, w.picnum);
        put_i16(out, w.overpicnum);
        out.push_back(static_cast<std::uint8_t>(w.shade));
        out.push_back(w.pal);
        out.push_back(w.xrepeat);
        out.push_back(w.yrepeat);
        out.push_back(w.xpanning);
        out.push_back(w.ypanning);
        put_i16(out, w.lotag);
        put_i16(out, w.hitag);
        put_i16(out, w.extra);
    }

    put_i16(out, static_cast<std::int16_t>(map.sprites.size()));
    for (const auto& sp : map.sprites) {
        put_i32(out, sp.x);
        put_i32(out, sp.y);
        put_i32(out, sp.z);
        put_i16(out, sp.cstat);
        put_i16(out, sp.picnum);
        out.push_back(static_cast<std::uint8_t>(sp.shade));
        out.push_back(sp.pal);
        out.push_back(sp.clipdist);
        out.push_back(sp.filler);
        out.push_back(sp.xrepeat);
        out.push_back(sp.yrepeat);
        out.push_back(static_cast<std::uint8_t>(sp.xoffset));
        out.push_back(static_cast<std::uint8_t>(sp.yoffset));
        put_i16(out, sp.sectnum);
        put_i16(out, sp.statnum);
        put_i16(out, sp.ang);
        put_i16(out, sp.owner);
        put_i16(out, sp.xvel);
        put_i16(out, sp.yvel);
        put_i16(out, sp.zvel);
        put_i16(out, sp.lotag);
        put_i16(out, sp.hitag);
        put_i16(out, sp.extra);
    }

    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

} // namespace fauxbuild
