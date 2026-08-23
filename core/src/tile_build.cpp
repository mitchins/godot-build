#include "fauxbuild/tile_build.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace fauxbuild {

namespace {

std::vector<std::string> split_fields(std::string_view line) {
    std::vector<std::string> fields;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
            ++i;
        }
        if (i > start) {
            fields.emplace_back(line.substr(start, i - start));
        }
    }
    return fields;
}

Result<long> to_long(const std::string& text, const std::string& source, std::size_t line_no,
                     const std::string& what) {
    try {
        std::size_t pos = 0;
        const long value = std::stol(text, &pos, 10);
        if (pos != text.size()) {
            throw std::invalid_argument("trailing");
        }
        return Result<long>::ok(value);
    } catch (const std::exception&) {
        return Result<long>::err({source, line_no, "tileset.field", ErrorCode::InvalidCount,
                                  what + " '" + text + "' is not an integer"});
    }
}

// key=value option extraction: pattern params use a=<n> style
Result<long> option_value(const std::vector<std::string>& fields, std::size_t start,
                          const std::string& key, long fallback, const std::string& source,
                          std::size_t line_no) {
    const std::string prefix = key + "=";
    for (std::size_t i = start; i < fields.size(); ++i) {
        if (fields[i].rfind(prefix, 0) == 0) {
            return to_long(fields[i].substr(prefix.size()), source, line_no, key);
        }
    }
    return Result<long>::ok(fallback);
}

std::uint8_t anim_code(const std::string& text) {
    if (text == "forward")
        return 2;
    if (text == "oscillating")
        return 1;
    if (text == "backward")
        return 3;
    return 255;
}

// Deterministic pattern generators. Pixel (x, y) -> palette index.
std::uint8_t pattern_pixel(const std::string& pattern, const std::vector<long>& p, std::int32_t x,
                           std::int32_t y, std::int32_t w, std::int32_t h, std::int32_t phase) {
    (void)h;
    if (pattern == "solid") {
        return static_cast<std::uint8_t>(p[0]);
    }
    if (pattern == "checker") {
        const long square = p.size() > 2 ? p[2] : 8;
        const long a = p[0];
        const long b = p[1];
        const auto cx = (x + phase * square) / square;
        return static_cast<std::uint8_t>(((cx + y / square) & 1) ? b : a);
    }
    if (pattern == "ramp") {
        const long from = p[0];
        const long to = p[1];
        const long span = (w > 1 ? w - 1 : 1);
        return static_cast<std::uint8_t>(from + (to - from) * x / span);
    }
    if (pattern == "grid") {
        const long major = p[0];
        const long line = p[1];
        const long bg = p[2];
        return static_cast<std::uint8_t>((x % major == 0 || y % major == 0) ? line : bg);
    }
    if (pattern == "indexed") {
        // Test strip: pixel value == its linear index (clamped). Intended for
        // w*h == 256 strips; general tiles get a folded index.
        return static_cast<std::uint8_t>((y * w + x + phase) & 0xFF);
    }
    return 0;
}

std::vector<long> pattern_params(const std::string& pattern, const std::vector<std::string>& fields,
                                 std::size_t start, const std::string& source, std::size_t line_no,
                                 bool& failed, ParseError& error) {
    std::vector<long> params;
    auto read = [&](const std::string& key, long fallback) -> long {
        auto value = option_value(fields, start, key, fallback, source, line_no);
        if (!value.is_ok()) {
            failed = true;
            error = value.error();
            return fallback;
        }
        return value.value();
    };
    if (pattern == "solid") {
        params.push_back(read("color", 0));
    } else if (pattern == "checker") {
        params.push_back(read("a", 1));
        params.push_back(read("b", 2));
        params.push_back(read("square", 8));
    } else if (pattern == "ramp") {
        params.push_back(read("from", 16));
        params.push_back(read("to", 31));
    } else if (pattern == "grid") {
        params.push_back(read("major", 8));
        params.push_back(read("line", 4));
        params.push_back(read("bg", 0));
    }
    return params;
}

ArtTile build_tile(const TilesetTile& tile, std::int32_t phase) {
    ArtTile out;
    out.width = tile.width;
    out.height = tile.height;
    out.meta.frames = tile.frames;
    out.meta.anim_type = tile.anim_type;
    out.meta.speed = tile.speed;
    out.meta.x_center = tile.x_center;
    out.meta.y_center = tile.y_center;
    // raw dword per the published picanm layout; round-tripped verbatim.
    const std::uint32_t raw =
        (static_cast<std::uint32_t>(tile.frames & 0x3F)) |
        (static_cast<std::uint32_t>(tile.anim_type & 0x3) << 6) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(tile.x_center)) << 8) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(tile.y_center)) << 16) |
        (static_cast<std::uint32_t>(tile.speed & 0xF) << 24);
    out.meta.raw = raw;
    out.pixels.resize(static_cast<std::size_t>(tile.width) * tile.height);
    // Emit in file order (column-major per the published description).
    std::size_t i = 0;
    for (std::int32_t x = 0; x < tile.width; ++x) {
        for (std::int32_t y = 0; y < tile.height; ++y) {
            out.pixels[i++] =
                pattern_pixel(tile.pattern, tile.params, x, y, tile.width, tile.height, phase);
        }
    }
    return out;
}

} // namespace

Result<TilesetDef> parse_tileset(std::string_view text, std::string source) {
    TilesetDef def;
    std::istringstream stream{std::string(text)};
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // CRLF files (Windows checkouts, real-world edits)
        }
        ++line_no;
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        auto fields = split_fields(line);
        if (fields.empty()) {
            continue;
        }
        const std::string& kind = fields[0];
        if (kind == "tileset" && fields.size() == 2) {
            def.name = fields[1];
            continue;
        }
        if (kind == "tile" || kind == "anim") {
            if (fields.size() < 4) {
                return Result<TilesetDef>::err({source, line_no, "tileset.line",
                                                ErrorCode::InvalidCount,
                                                kind + " needs: name w h pattern=..."});
            }
            TilesetTile tile;
            tile.name = fields[1];
            auto w = to_long(fields[2], source, line_no, "width");
            if (!w.is_ok())
                return Result<TilesetDef>::err(w.error());
            tile.width = static_cast<std::int16_t>(w.value());
            auto h = to_long(fields[3], source, line_no, "height");
            if (!h.is_ok())
                return Result<TilesetDef>::err(h.error());
            tile.height = static_cast<std::int16_t>(h.value());
            if (tile.width <= 0 || tile.height <= 0) {
                return Result<TilesetDef>::err(
                    {source, line_no, "tileset.line", ErrorCode::InvalidCount,
                     "tile '" + tile.name + "' must have positive dimensions"});
            }
            std::string pattern_field;
            for (std::size_t i = 4; i < fields.size(); ++i) {
                if (fields[i].rfind("pattern=", 0) == 0) {
                    pattern_field = fields[i].substr(8);
                    fields.erase(fields.begin() + static_cast<long>(i));
                    break;
                }
            }
            if (pattern_field.empty()) {
                return Result<TilesetDef>::err(
                    {source, line_no, "tileset.line", ErrorCode::InvalidName,
                     "tile '" + tile.name + "' has no pattern=... directive"});
            }
            tile.pattern = pattern_field;
            if (kind == "anim") {
                auto frames = option_value(fields, 4, "frames", 0, source, line_no);
                if (!frames.is_ok())
                    return Result<TilesetDef>::err(frames.error());
                if (frames.value() < 2 || frames.value() > 63) {
                    return Result<TilesetDef>::err(
                        {source, line_no, "tileset.line", ErrorCode::InvalidCount,
                         "anim '" + tile.name + "' frames must be 2..63"});
                }
                tile.frames = static_cast<std::uint8_t>(frames.value());
                std::string type;
                for (std::size_t i = 4; i < fields.size(); ++i) {
                    if (fields[i].rfind("type=", 0) == 0) {
                        type = fields[i].substr(5);
                        break;
                    }
                }
                const std::uint8_t code = anim_code(type);
                if (code == 255) {
                    return Result<TilesetDef>::err({source, line_no, "tileset.line",
                                                    ErrorCode::InvalidName,
                                                    "anim '" + tile.name +
                                                        "' type must be forward|oscillating|"
                                                        "backward"});
                }
                tile.anim_type = code;
                auto speed = option_value(fields, 4, "speed", 0, source, line_no);
                if (!speed.is_ok())
                    return Result<TilesetDef>::err(speed.error());
                tile.speed = static_cast<std::uint8_t>(speed.value());
            }
            bool param_failed = false;
            ParseError param_error;
            tile.params = pattern_params(pattern_field, fields, 4, source, line_no, param_failed,
                                         param_error);
            if (param_failed) {
                return Result<TilesetDef>::err(param_error);
            }
            def.tiles.push_back(std::move(tile));
            continue;
        }
        if (kind == "pivot" && fields.size() == 4) {
            if (def.tiles.empty() || def.tiles.back().name != fields[1]) {
                return Result<TilesetDef>::err(
                    {source, line_no, "tileset.pivot", ErrorCode::InvalidName,
                     "pivot '" + fields[1] + "' must directly follow its tile"});
            }
            auto xc = to_long(fields[2], source, line_no, "x_center");
            if (!xc.is_ok())
                return Result<TilesetDef>::err(xc.error());
            auto yc = to_long(fields[3], source, line_no, "y_center");
            if (!yc.is_ok())
                return Result<TilesetDef>::err(yc.error());
            def.tiles.back().x_center = static_cast<std::int8_t>(xc.value());
            def.tiles.back().y_center = static_cast<std::int8_t>(yc.value());
            continue;
        }
        return Result<TilesetDef>::err({source, line_no, "tileset.line", ErrorCode::InvalidName,
                                        "unknown directive '" + kind + "'"});
    }
    if (def.name.empty()) {
        return Result<TilesetDef>::err({source, 0, "tileset.header", ErrorCode::InvalidName,
                                        "missing 'tileset <name>' header"});
    }
    for (std::size_t i = 0; i < def.tiles.size(); ++i) {
        for (std::size_t j = i + 1; j < def.tiles.size(); ++j) {
            if (def.tiles[i].name == def.tiles[j].name) {
                return Result<TilesetDef>::err({source, 0, "tileset.validate",
                                                ErrorCode::InvalidName,
                                                "duplicate tile name '" + def.tiles[i].name + "'"});
            }
        }
    }
    return Result<TilesetDef>::ok(std::move(def));
}

Result<BuiltArt> build_art_from_tileset(const TilesetDef& tileset, const TileManifest& manifest) {
    // Stability pass 1: every manifest entry must resolve to a tileset tile
    // with identical shape; assignments are immutable. Frame entries carry
    // the base tile name plus '#k'; only the anchor (#0) records the frame
    // count and animation type.
    auto base_name = [](const std::string& name) {
        const auto hash = name.rfind('#');
        return hash == std::string::npos ? name : name.substr(0, hash);
    };
    auto frame_index = [](const std::string& name) -> std::int32_t {
        const auto hash = name.rfind('#');
        if (hash == std::string::npos) {
            return -1; // not a frame entry
        }
        try {
            return std::stoi(name.substr(hash + 1));
        } catch (const std::exception&) {
            return -2; // malformed suffix
        }
    };
    for (const auto& entry : manifest.entries) {
        const std::string base = base_name(entry.name);
        const std::int32_t frame = frame_index(entry.name);
        if (frame == -2) {
            return Result<BuiltArt>::err(
                {"tileset", 0, "build.stability", ErrorCode::InvalidName,
                 "manifest entry '" + entry.name + "' has a malformed frame suffix"});
        }
        const TilesetTile* source = nullptr;
        for (const auto& tile : tileset.tiles) {
            if (tile.name == base) {
                source = &tile;
                break;
            }
        }
        if (source == nullptr) {
            return Result<BuiltArt>::err(
                {"tileset", 0, "build.stability", ErrorCode::InvalidName,
                 "tile '" + entry.name +
                     "' is in the manifest but missing from the tileset; removing a "
                     "tile changes picnum meaning and is forbidden"});
        }
        if (frame == -1 && source->frames > 1) {
            return Result<BuiltArt>::err(
                {"tileset", 0, "build.stability", ErrorCode::InvalidName,
                 "manifest entry '" + entry.name +
                     "' lost its frame suffix; animation sets must stay sets"});
        }
        if (frame >= 0 && source->frames <= 1) {
            return Result<BuiltArt>::err(
                {"tileset", 0, "build.stability", ErrorCode::InvalidName,
                 "manifest entry '" + entry.name +
                     "' has a frame suffix but the tile is not an animation set"});
        }
        // Recorded convention: singles -> frames 0/anim 0; anchors (#0) ->
        // the set's frames and type; other frames -> 0/0.
        const bool anchor = frame == 0 || (frame == -1 && source->frames <= 1);
        const std::uint8_t recorded_frames = (anchor && source->frames > 1) ? source->frames : 0;
        const std::uint8_t recorded_anim = (anchor && source->frames > 1) ? source->anim_type : 0;
        if (source->width != entry.width || source->height != entry.height ||
            source->x_center != entry.x_center || source->y_center != entry.y_center ||
            recorded_anim != entry.anim_type || recorded_frames != entry.frames ||
            source->speed != entry.speed) {
            return Result<BuiltArt>::err(
                {"tileset", 0, "build.stability", ErrorCode::InvalidName,
                 "tile '" + entry.name +
                     "' changed shape/pivot/animation vs the manifest; entries are "
                     "immutable once assigned"});
        }
    }

    // Stability pass 2: assign picnums for new tiles (append-only, max+1).
    TileManifest updated = manifest;
    for (const auto& tile : tileset.tiles) {
        const bool already_assigned = tile.frames > 1 ? updated.find(tile.name + "#0") != nullptr
                                                      : updated.find(tile.name) != nullptr;
        if (already_assigned) {
            continue;
        }
        // An animation set occupies `frames` consecutive picnums as entries
        // name#k; only the anchor (#0) records the frame count and type, the
        // same convention enforced by the stability pass. Singles record 0/0.
        for (std::uint8_t frame = 0; frame < tile.frames; ++frame) {
            const std::string frame_name =
                tile.frames > 1 ? tile.name + "#" + std::to_string(frame) : tile.name;
            const bool anchor = tile.frames <= 1 || frame == 0;
            const std::uint8_t recorded_frames = anchor && tile.frames > 1 ? tile.frames : 0;
            const std::uint8_t recorded_anim = anchor && tile.frames > 1 ? tile.anim_type : 0;
            auto assigned =
                updated.assign(frame_name, tile.width, tile.height, tile.x_center, tile.y_center,
                               recorded_anim, recorded_frames, tile.speed);
            if (!assigned.is_ok()) {
                return Result<BuiltArt>::err(assigned.error());
            }
        }
    }

    // Emit tiles in picnum order.
    BuiltArt built;
    built.manifest = updated;
    built.art.localtilestart = 0;
    built.art.localtileend = static_cast<std::int32_t>(updated.entries.size()) - 1;
    built.art.numtiles_field = static_cast<std::int32_t>(updated.entries.size());
    built.art.version = 1;
    built.art.tiles.reserve(updated.entries.size());

    for (const auto& entry : updated.entries) {
        std::string tile_name = entry.name;
        std::int32_t phase = 0;
        const auto hash = entry.name.rfind('#');
        if (hash != std::string::npos) {
            tile_name = entry.name.substr(0, hash);
            phase = std::stoi(entry.name.substr(hash + 1));
        }
        const TilesetTile* source = nullptr;
        for (const auto& tile : tileset.tiles) {
            if (tile.name == tile_name) {
                source = &tile;
                break;
            }
        }
        if (source == nullptr) {
            return Result<BuiltArt>::err(
                {"tileset", 0, "build.emit", ErrorCode::InvalidName,
                 "manifest tile '" + entry.name + "' has no source definition"});
        }
        ArtTile out = build_tile(*source, phase);
        if (source->frames > 1) {
            out.meta.frames = 0; // per-frame tiles carry the frame data; the
                                 // animation range is span + type on the anchor
        } else {
            out.meta.anim_type = 0;
            out.meta.frames = 0;
        }
        built.art.tiles.push_back(std::move(out));
    }
    // Anchor semantics: frame 0 of each set carries frames/type (Build
    // convention observed in real content: animated tiles carry frames>0).
    for (const auto& tile : tileset.tiles) {
        if (tile.frames <= 1) {
            continue;
        }
        const auto* anchor = built.manifest.find(tile.name + "#0");
        if (anchor == nullptr) {
            continue;
        }
        built.art.tiles[static_cast<std::size_t>(anchor->picnum)].meta.frames = tile.frames;
        built.art.tiles[static_cast<std::size_t>(anchor->picnum)].meta.anim_type = tile.anim_type;
    }
    return Result<BuiltArt>::ok(std::move(built));
}

Result<PaletteSpec> parse_palette_spec(std::string_view text, std::string source) {
    PaletteSpec spec;
    std::istringstream stream{std::string(text)};
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // CRLF files (Windows checkouts, real-world edits)
        }
        ++line_no;
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        const auto fields = split_fields(line);
        if (fields.empty()) {
            continue;
        }
        if (fields[0] == "palette" && fields.size() == 2) {
            spec.name = fields[1];
            continue;
        }
        if (fields[0] == "shades" && fields.size() == 2) {
            auto count = to_long(fields[1], source, line_no, "shades");
            if (!count.is_ok())
                return Result<PaletteSpec>::err(count.error());
            if (count.value() < 1 || count.value() > 64) {
                return Result<PaletteSpec>::err({source, line_no, "palette.line",
                                                 ErrorCode::InvalidCount, "shades must be 1..64"});
            }
            spec.shades = static_cast<std::int32_t>(count.value());
            continue;
        }
        if (fields[0] == "translucent" && fields.size() == 2) {
            spec.translucent = fields[1] == "on";
            continue;
        }
        if (fields[0] == "entry" && fields.size() == 5) {
            auto index = to_long(fields[1], source, line_no, "index");
            if (!index.is_ok())
                return Result<PaletteSpec>::err(index.error());
            if (index.value() < 0 || index.value() > 255) {
                return Result<PaletteSpec>::err({source, line_no, "palette.line",
                                                 ErrorCode::OutOfBounds,
                                                 "palette index out of range"});
            }
            for (int c = 0; c < 3; ++c) {
                auto value = to_long(fields[2 + c], source, line_no, "component");
                if (!value.is_ok())
                    return Result<PaletteSpec>::err(value.error());
                if (value.value() < 0 || value.value() > 63) {
                    return Result<PaletteSpec>::err({source, line_no, "palette.line",
                                                     ErrorCode::OutOfBounds,
                                                     "components are 6-bit (0..63)"});
                }
                spec.entry[index.value()][c] = static_cast<std::int32_t>(value.value());
            }
            spec.entry_set[index.value()] = true;
            continue;
        }
        if (fields[0] == "ramp" && fields.size() == 10 && fields[6] == "->") {
            PaletteSpec::Ramp ramp;
            auto start = to_long(fields[1], source, line_no, "start");
            if (!start.is_ok())
                return Result<PaletteSpec>::err(start.error());
            auto count = to_long(fields[2], source, line_no, "count");
            if (!count.is_ok())
                return Result<PaletteSpec>::err(count.error());
            if (start.value() < 0 || count.value() < 1 || start.value() + count.value() > 256) {
                return Result<PaletteSpec>::err({source, line_no, "palette.ramp",
                                                 ErrorCode::OutOfBounds,
                                                 "ramp range out of palette bounds"});
            }
            ramp.start = static_cast<std::int32_t>(start.value());
            ramp.count = static_cast<std::int32_t>(count.value());
            for (int c = 0; c < 3; ++c) {
                auto from = to_long(fields[3][c] == 'x' ? "0" : std::string(1, fields[3][c]),
                                    source, line_no, "from");
                (void)from;
            }
            // parse "<r g b> -> <r g b>": fields are 3,4('->'),5,6,7 and 8,9
            // layout: ramp <start> <count> <r> <g> <b> -> <r> <g> <b>
            for (int c = 0; c < 3; ++c) {
                auto from = to_long(fields[3 + c], source, line_no, "from");
                if (!from.is_ok())
                    return Result<PaletteSpec>::err(from.error());
                auto to = to_long(fields[7 + c], source, line_no, "to");
                if (!to.is_ok())
                    return Result<PaletteSpec>::err(to.error());
                if (from.value() < 0 || from.value() > 63 || to.value() < 0 || to.value() > 63) {
                    return Result<PaletteSpec>::err({source, line_no, "palette.ramp",
                                                     ErrorCode::OutOfBounds,
                                                     "ramp components are 6-bit (0..63)"});
                }
                ramp.from[c] = static_cast<std::int32_t>(from.value());
                ramp.to[c] = static_cast<std::int32_t>(to.value());
            }
            spec.ramps.push_back(ramp);
            continue;
        }
        if (fields[0] == "swap" && fields.size() == 6 && fields[2] == "tint") {
            PaletteSpec::Swap swap;
            auto index = to_long(fields[1], source, line_no, "index");
            if (!index.is_ok())
                return Result<PaletteSpec>::err(index.error());
            if (index.value() < 1 || index.value() > 255) {
                return Result<PaletteSpec>::err(
                    {source, line_no, "palette.swap", ErrorCode::OutOfBounds,
                     "swap index must be 1..255 (0 is the base palette)"});
            }
            swap.index = static_cast<std::uint8_t>(index.value());
            for (int c = 0; c < 3; ++c) {
                auto value = to_long(fields[3 + c], source, line_no, "tint");
                if (!value.is_ok())
                    return Result<PaletteSpec>::err(value.error());
                if (value.value() < 0 || value.value() > 63) {
                    return Result<PaletteSpec>::err({source, line_no, "palette.swap",
                                                     ErrorCode::OutOfBounds,
                                                     "tint components are 6-bit (0..63)"});
                }
                swap.tint[c] = static_cast<std::int32_t>(value.value());
            }
            spec.swaps.push_back(swap);
            continue;
        }
        return Result<PaletteSpec>::err({source, line_no, "palette.line", ErrorCode::InvalidName,
                                         "unknown directive '" + fields[0] + "'"});
    }
    if (spec.name.empty()) {
        return Result<PaletteSpec>::err({source, 0, "palette.header", ErrorCode::InvalidName,
                                         "missing 'palette <name>' header"});
    }
    // Apply ramps over unset entries.
    for (const auto& ramp : spec.ramps) {
        for (std::int32_t i = 0; i < ramp.count; ++i) {
            const std::int32_t index = ramp.start + i;
            const std::int32_t denom = std::max<std::int32_t>(1, ramp.count - 1);
            for (int c = 0; c < 3; ++c) {
                spec.entry[index][c] = ramp.from[c] + (ramp.to[c] - ramp.from[c]) * i / denom;
                spec.entry_set[index] = true;
            }
        }
    }
    long unset = 0;
    for (std::int32_t i = 0; i < 256; ++i) {
        if (!spec.entry_set[i]) {
            ++unset;
        }
    }
    if (unset > 0) {
        return Result<PaletteSpec>::err(
            {source, 0, "palette.coverage", ErrorCode::InvalidCount,
             std::to_string(unset) + " of 256 palette entries are undefined"});
    }
    return Result<PaletteSpec>::ok(std::move(spec));
}

namespace {

std::int64_t luminance(const PaletteSpec& spec, std::int32_t index) {
    // Integer luminance approximation; deterministic, original.
    return (spec.entry[index][0] * 299 + spec.entry[index][1] * 587 + spec.entry[index][2] * 114);
}

std::int32_t nearest_index(const PaletteSpec& spec, double r, double g, double b) {
    std::int32_t best = 0;
    double best_distance = 1e30;
    for (std::int32_t i = 0; i < 256; ++i) {
        const double dr = spec.entry[i][0] - r;
        const double dg = spec.entry[i][1] - g;
        const double db = spec.entry[i][2] - b;
        const double distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

} // namespace

Result<PaletteData> build_palette_dat(const PaletteSpec& spec) {
    PaletteData data;
    for (std::int32_t i = 0; i < 256; ++i) {
        for (int c = 0; c < 3; ++c) {
            data.rgb[static_cast<std::size_t>(i * 3 + c)] =
                static_cast<std::uint8_t>(spec.entry[i][c]);
        }
    }
    data.num_shades = static_cast<std::int16_t>(spec.shades);
    data.shade_tables.resize(static_cast<std::size_t>(spec.shades) * 256);

    for (std::int32_t level = 0; level < spec.shades; ++level) {
        const double factor = static_cast<double>(spec.shades - 1 - level) /
                              static_cast<double>(std::max(1, spec.shades - 1));
        for (std::int32_t i = 0; i < 256; ++i) {
            if (level == spec.shades - 1) {
                // Darkest level maps everything to the darkest entry.
                std::int32_t darkest = 0;
                std::int64_t darkest_lum = luminance(spec, 0);
                for (std::int32_t j = 1; j < 256; ++j) {
                    const std::int64_t lum = luminance(spec, j);
                    if (lum < darkest_lum) {
                        darkest_lum = lum;
                        darkest = j;
                    }
                }
                data.shade_tables[static_cast<std::size_t>(level * 256 + i)] =
                    static_cast<std::uint8_t>(darkest);
                continue;
            }
            const double target_r = spec.entry[i][0] * factor;
            const double target_g = spec.entry[i][1] * factor;
            const double target_b = spec.entry[i][2] * factor;
            data.shade_tables[static_cast<std::size_t>(level * 256 + i)] =
                static_cast<std::uint8_t>(nearest_index(spec, target_r, target_g, target_b));
        }
    }

    if (spec.translucent) {
        data.translucency.resize(kTranslucencyBytes);
        for (std::int32_t a = 0; a < 256; ++a) {
            for (std::int32_t b = 0; b < 256; ++b) {
                const double r = (spec.entry[a][0] + spec.entry[b][0]) / 2.0;
                const double g = (spec.entry[a][1] + spec.entry[b][1]) / 2.0;
                const double bb = (spec.entry[a][2] + spec.entry[b][2]) / 2.0;
                data.translucency[static_cast<std::size_t>(a * 256 + b)] =
                    static_cast<std::uint8_t>(nearest_index(spec, r, g, bb));
            }
        }
    } else {
        data.translucency.assign(kTranslucencyBytes, 0);
    }
    return Result<PaletteData>::ok(std::move(data));
}

Result<LookupData> build_lookup_dat(const PaletteSpec& spec) {
    LookupData data;
    for (const auto& swap : spec.swaps) {
        LookupSwap out;
        out.index = swap.index;
        for (std::int32_t i = 0; i < 256; ++i) {
            // Tint = blend the entry toward the tint colour by 50%, then
            // snap to the nearest palette entry. Deterministic and original.
            const double r = (spec.entry[i][0] + swap.tint[0]) / 2.0;
            const double g = (spec.entry[i][1] + swap.tint[1]) / 2.0;
            const double b = (spec.entry[i][2] + swap.tint[2]) / 2.0;
            out.table[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(nearest_index(spec, r, g, b));
        }
        data.swaps.push_back(out);
    }
    return Result<LookupData>::ok(std::move(data));
}

} // namespace fauxbuild
