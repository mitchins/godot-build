#include "fauxbuild/asset_set.hpp"

#include <algorithm>

namespace fauxbuild {

bool is_art_name(std::string_view name) {
    constexpr std::string_view kPrefix = "TILES";
    constexpr std::string_view kSuffix = ".ART";
    return name.size() > kPrefix.size() + kSuffix.size() &&
           name.substr(0, kPrefix.size()) == kPrefix &&
           name.substr(name.size() - kSuffix.size()) == kSuffix;
}

Result<AssetSet> load_asset_set(const Vfs& vfs) {
    const auto keys = vfs.keys(); // deduplicated union, normalized

    std::vector<std::string> art_names;
    bool has_palette = false;
    bool has_lookup = false;
    for (const auto& key : keys) {
        if (is_art_name(key)) {
            art_names.push_back(key);
        } else if (key == "PALETTE.DAT") {
            has_palette = true;
        } else if (key == "LOOKUP.DAT") {
            has_lookup = true;
        }
    }

    if (art_names.empty()) {
        return Result<AssetSet>::err({"asset_set", 0, "asset_set.arts", ErrorCode::NotFound,
                                      "no TILES*.ART file found in any mount"});
    }
    if (!has_palette) {
        return Result<AssetSet>::err({"asset_set", 0, "asset_set.palette", ErrorCode::NotFound,
                                      "PALETTE.DAT not found in any mount"});
    }
    if (!has_lookup) {
        return Result<AssetSet>::err({"asset_set", 0, "asset_set.lookup", ErrorCode::NotFound,
                                      "LOOKUP.DAT not found in any mount"});
    }

    AssetSet set;

    auto palette_file = vfs.open("PALETTE.DAT");
    if (!palette_file.is_ok()) {
        return Result<AssetSet>::err(palette_file.error());
    }
    {
        const auto& bytes = palette_file.value().bytes;
        auto parsed = read_palette_dat(
            std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
            palette_file.value().origin);
        if (!parsed.is_ok()) {
            return Result<AssetSet>::err(parsed.error());
        }
        set.palette = parsed.take();
    }

    auto lookup_file = vfs.open("LOOKUP.DAT");
    if (!lookup_file.is_ok()) {
        return Result<AssetSet>::err(lookup_file.error());
    }
    {
        const auto& bytes = lookup_file.value().bytes;
        auto parsed = read_lookup_dat(
            std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
            lookup_file.value().origin);
        if (!parsed.is_ok()) {
            return Result<AssetSet>::err(parsed.error());
        }
        set.lookup = parsed.take();
    }

    // Sort by name first so the later range sort is stable for equal
    // starts (which build_indexed_atlas rejects as overlaps anyway — but
    // error determinism is still determinism).
    std::sort(art_names.begin(), art_names.end());
    struct Loaded {
        ArtData art;
        std::string name;
    };
    std::vector<Loaded> loaded;
    loaded.reserve(art_names.size());
    for (const auto& name : art_names) {
        auto file = vfs.open(name);
        if (!file.is_ok()) {
            return Result<AssetSet>::err(file.error());
        }
        const auto& bytes = file.value().bytes;
        auto parsed =
            read_art(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                     file.value().origin);
        if (!parsed.is_ok()) {
            return Result<AssetSet>::err(parsed.error());
        }
        loaded.push_back(Loaded{parsed.take(), name});
    }
    std::sort(loaded.begin(), loaded.end(), [](const Loaded& a, const Loaded& b) {
        return std::make_pair(a.art.localtilestart, a.name) <
               std::make_pair(b.art.localtilestart, b.name);
    });
    for (auto& entry : loaded) {
        set.art_names.push_back(std::move(entry.name));
        set.arts.push_back(std::move(entry.art));
    }

    return Result<AssetSet>::ok(std::move(set));
}

} // namespace fauxbuild
