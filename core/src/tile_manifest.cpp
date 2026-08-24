#include "fauxbuild/tile_manifest.hpp"

#include <cstdio>

#include <algorithm>
#include <sstream>

#include "fauxbuild/map_io.hpp"

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

Result<long> parse_int(const std::string& text, const std::string& source, std::size_t line_no,
                       const std::string& field) {
    try {
        std::size_t pos = 0;
        const long value = std::stol(text, &pos, 10);
        if (pos != text.size()) {
            throw std::invalid_argument("trailing");
        }
        return Result<long>::ok(value);
    } catch (const std::exception&) {
        return Result<long>::err({source, line_no, "manifest.field", ErrorCode::InvalidCount,
                                  field + " '" + text + "' is not an integer"});
    }
}

std::string hex64(std::uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buffer, 16);
}

std::string anim_name(std::uint8_t type) {
    switch (type) {
    case 1:
        return "oscillating";
    case 2:
        return "forward";
    case 3:
        return "backward";
    default:
        return "none";
    }
}

std::uint8_t anim_code(const std::string& text) {
    if (text == "none")
        return 0;
    if (text == "oscillating")
        return 1;
    if (text == "forward")
        return 2;
    if (text == "backward")
        return 3;
    return 255; // rejected by the caller
}

} // namespace

const TileManifestEntry* TileManifest::find(const std::string& name) const {
    for (const auto& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

const TileManifestEntry* TileManifest::find_picnum(std::int32_t picnum) const {
    for (const auto& entry : entries) {
        if (entry.picnum == picnum) {
            return &entry;
        }
    }
    return nullptr;
}

Result<std::int32_t> TileManifest::assign(const std::string& name, std::int16_t width,
                                          std::int16_t height, std::int8_t x_center,
                                          std::int8_t y_center, std::uint8_t anim_type,
                                          std::uint8_t frames, std::uint8_t speed,
                                          std::uint64_t content) {
    if (find(name) != nullptr) {
        return Result<std::int32_t>::err(
            {"manifest", 0, "manifest.assign", ErrorCode::InvalidName,
             "tile '" + name + "' already has a picnum; assignments are immutable"});
    }
    std::int32_t next = 0;
    for (const auto& entry : entries) {
        next = std::max(next, entry.picnum + 1);
    }
    entries.push_back(
        {next, name, width, height, x_center, y_center, anim_type, frames, speed, content});
    return Result<std::int32_t>::ok(next);
}

Result<TileManifest> parse_tile_manifest(std::string_view text, std::string source) {
    TileManifest manifest;
    std::istringstream stream{std::string(text)};
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // CRLF files (Windows checkouts, real-world edits)
        }
        ++line_no;
        // Comments are whole lines only (leading '#'): generated animation
        // frame names contain '#' (name#frame) and must survive intact.
        const auto first_nonspace = line.find_first_not_of(" \t");
        if (first_nonspace != std::string::npos && line[first_nonspace] == '#') {
            continue;
        }
        const auto fields = split_fields(line);
        if (fields.empty()) {
            continue;
        }
        if (fields.size() != 10) {
            return Result<TileManifest>::err(
                {source, line_no, "manifest.line", ErrorCode::InvalidCount,
                 "expected 10 fields (picnum name w h xc yc anim frames speed content), got " +
                     std::to_string(fields.size())});
        }
        TileManifestEntry entry;
        auto picnum = parse_int(fields[0], source, line_no, "picnum");
        if (!picnum.is_ok())
            return Result<TileManifest>::err(picnum.error());
        entry.picnum = static_cast<std::int32_t>(picnum.value());
        entry.name = fields[1];
        // Range-check before narrowing: width 70000 silently became 4464 as an
        // int16 and then passed validate_tile_manifest, which only sees the
        // narrowed value.
        auto in_range = [](const Result<long>& v, long lo, long hi) -> bool {
            return v.is_ok() && v.value() >= lo && v.value() <= hi;
        };
        auto range_error = [&](const char* what, long lo, long hi) {
            return Result<TileManifest>::err(
                {source, line_no, "manifest.line", ErrorCode::OutOfBounds,
                 std::string(what) + " is out of range [" + std::to_string(lo) + ", " +
                     std::to_string(hi) + "]"});
        };
        auto w = parse_int(fields[2], source, line_no, "width");
        if (!w.is_ok())
            return Result<TileManifest>::err(w.error());
        if (!in_range(w, 0, 32767))
            return range_error("width", 0, 32767);
        entry.width = static_cast<std::int16_t>(w.value());
        auto h = parse_int(fields[3], source, line_no, "height");
        if (!h.is_ok())
            return Result<TileManifest>::err(h.error());
        if (!in_range(h, 0, 32767))
            return range_error("height", 0, 32767);
        entry.height = static_cast<std::int16_t>(h.value());
        auto xc = parse_int(fields[4], source, line_no, "x_center");
        if (!xc.is_ok())
            return Result<TileManifest>::err(xc.error());
        if (!in_range(xc, -128, 127))
            return range_error("x_center", -128, 127);
        entry.x_center = static_cast<std::int8_t>(xc.value());
        auto yc = parse_int(fields[5], source, line_no, "y_center");
        if (!yc.is_ok())
            return Result<TileManifest>::err(yc.error());
        if (!in_range(yc, -128, 127))
            return range_error("y_center", -128, 127);
        entry.y_center = static_cast<std::int8_t>(yc.value());
        const std::uint8_t anim = anim_code(fields[6]);
        if (anim == 255) {
            return Result<TileManifest>::err({source, line_no, "manifest.line",
                                              ErrorCode::InvalidName,
                                              "unknown animation type '" + fields[6] + "'"});
        }
        entry.anim_type = anim;
        auto frames = parse_int(fields[7], source, line_no, "frames");
        if (!frames.is_ok())
            return Result<TileManifest>::err(frames.error());
        if (!in_range(frames, 0, 255))
            return range_error("frames", 0, 255);
        entry.frames = static_cast<std::uint8_t>(frames.value());
        auto speed = parse_int(fields[8], source, line_no, "speed");
        if (!speed.is_ok())
            return Result<TileManifest>::err(speed.error());
        if (!in_range(speed, 0, 255))
            return range_error("speed", 0, 255);
        entry.speed = static_cast<std::uint8_t>(speed.value());
        // Content hash: exactly 16 lowercase hex digits, no 0x, no sign. Strict
        // because this field is the immutability anchor.
        const std::string& hex = fields[9];
        if (hex.size() != 16 || hex.find_first_not_of("0123456789abcdef") != std::string::npos) {
            return Result<TileManifest>::err(
                {source, line_no, "manifest.line", ErrorCode::InvalidName,
                 "content hash must be 16 lowercase hex digits, got '" + hex + "'"});
        }
        entry.content = std::stoull(hex, nullptr, 16);
        manifest.entries.push_back(std::move(entry));
    }

    auto valid = validate_tile_manifest(manifest);
    if (!valid.is_ok()) {
        ParseError error = valid.error();
        error.offset = 0;
        return Result<TileManifest>::err(error);
    }
    return Result<TileManifest>::ok(std::move(manifest));
}

Result<std::string> write_tile_manifest(const TileManifest& manifest) {
    auto valid = validate_tile_manifest(manifest);
    if (!valid.is_ok()) {
        return Result<std::string>::err(valid.error());
    }
    std::string out = "# fauxbuild tile manifest v2\n";
    out += "# picnum  name  w  h  xc  yc  anim  frames  speed  content\n";
    for (const auto& entry : manifest.entries) {
        out += std::to_string(entry.picnum) + "  " + entry.name + "  " +
               std::to_string(entry.width) + " " + std::to_string(entry.height) + "  " +
               std::to_string(entry.x_center) + " " + std::to_string(entry.y_center) + "  " +
               anim_name(entry.anim_type) + " " + std::to_string(entry.frames) + " " +
               std::to_string(entry.speed) + "  " + hex64(entry.content) + "\n";
    }
    return Result<std::string>::ok(std::move(out));
}

Result<void> validate_tile_manifest(const TileManifest& manifest) {
    std::int32_t expected = 0;
    for (const auto& entry : manifest.entries) {
        if (entry.picnum != expected) {
            return Result<void>::err({"manifest", 0, "manifest.validate", ErrorCode::InvalidCount,
                                      "picnum " + std::to_string(entry.picnum) +
                                          " breaks the gapless ascending sequence (expected " +
                                          std::to_string(expected) + ")"});
        }
        ++expected;
        if (entry.width < 0 || entry.height < 0) {
            return Result<void>::err({"manifest", 0, "manifest.validate", ErrorCode::InvalidCount,
                                      "tile '" + entry.name + "' has negative dimensions"});
        }
        if (entry.anim_type > 3) {
            return Result<void>::err({"manifest", 0, "manifest.validate", ErrorCode::InvalidName,
                                      "tile '" + entry.name + "' has unknown animation type"});
        }
    }
    for (std::size_t i = 0; i < manifest.entries.size(); ++i) {
        for (std::size_t j = i + 1; j < manifest.entries.size(); ++j) {
            if (manifest.entries[i].name == manifest.entries[j].name) {
                return Result<void>::err(
                    {"manifest", 0, "manifest.validate", ErrorCode::InvalidName,
                     "duplicate tile name '" + manifest.entries[i].name + "'"});
            }
        }
    }
    return Result<void>::ok();
}

} // namespace fauxbuild
