#include "fauxbuild_godot/faux_asset_set.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstring>

#include "fauxbuild/vfs.hpp"

namespace fauxbuild_godot {

namespace {

// Vfs is movable but holds unique_ptr mounts; build it in place.
bool mount_and_load(fauxbuild::Vfs& vfs, fauxbuild::AssetSet& set_out,
                    fauxbuild::IndexedAtlas& atlas_out, godot::String& error_out,
                    std::int32_t page_width, std::int32_t page_height) {
    auto set = fauxbuild::load_asset_set(vfs);
    if (!set.is_ok()) {
        error_out = godot::String(set.error().to_string().c_str());
        return false;
    }
    // Page size is a deployment parameter (GPU limits differ), so it is
    // settable rather than baked in. 0 keeps the default. It is also what
    // makes the multi-page path reachable from the boundary test without
    // proprietary content: small pages force ordinary fixture tiles onto
    // page 1 (review, PR #4 — the one-page fixture left page rebinding
    // covered only by a human running gate A).
    fauxbuild::AtlasOptions options;
    if (page_width > 0) {
        options.page_width = page_width;
    }
    if (page_height > 0) {
        options.page_height = page_height;
    }
    auto atlas = fauxbuild::build_indexed_atlas(set.value().arts, options);
    if (!atlas.is_ok()) {
        error_out = godot::String(atlas.error().to_string().c_str());
        return false;
    }
    set_out = set.take();
    atlas_out = atlas.take();
    return true;
}

} // namespace

bool FauxAssetSet::load_dir(const godot::String& path, int page_width, int page_height) {
    auto mount = fauxbuild::DirectoryMount::create(path.utf8().get_data());
    if (!mount.is_ok()) {
        last_error_ = godot::String(mount.error().to_string().c_str());
        return false;
    }
    fauxbuild::Vfs vfs;
    vfs.add_mount(mount.take());
    set_ = std::make_shared<fauxbuild::AssetSet>();
    atlas_ = std::make_shared<fauxbuild::IndexedAtlas>();
    if (!mount_and_load(vfs, *set_, *atlas_, last_error_, page_width, page_height)) {
        set_.reset();
        atlas_.reset();
        return false;
    }
    return true;
}

bool FauxAssetSet::load_grp(const godot::String& path, int page_width, int page_height) {
    auto mount = fauxbuild::GrpMount::create(path.utf8().get_data());
    if (!mount.is_ok()) {
        last_error_ = godot::String(mount.error().to_string().c_str());
        return false;
    }
    fauxbuild::Vfs vfs;
    vfs.add_mount(mount.take());
    set_ = std::make_shared<fauxbuild::AssetSet>();
    atlas_ = std::make_shared<fauxbuild::IndexedAtlas>();
    if (!mount_and_load(vfs, *set_, *atlas_, last_error_, page_width, page_height)) {
        set_.reset();
        atlas_.reset();
        return false;
    }
    return true;
}

bool FauxAssetSet::is_loaded() const {
    return set_ != nullptr && atlas_ != nullptr;
}

godot::String FauxAssetSet::get_last_error() const {
    return last_error_;
}

std::int32_t FauxAssetSet::get_tile_count() const {
    return atlas_ ? static_cast<std::int32_t>(atlas_->tile_count) : 0;
}

godot::Vector2i FauxAssetSet::get_page_size() const {
    return atlas_ ? godot::Vector2i(atlas_->page_width, atlas_->page_height) : godot::Vector2i();
}

std::int32_t FauxAssetSet::get_page_count() const {
    return atlas_ ? atlas_->page_count : 0;
}

godot::Rect2i FauxAssetSet::get_tile_rect(std::int32_t picnum) const {
    if (!atlas_ || picnum < 0 || static_cast<std::uint32_t>(picnum) >= atlas_->tiles.size()) {
        return godot::Rect2i();
    }
    const auto& t = atlas_->tiles[picnum];
    return godot::Rect2i(t.x, t.y, t.width, t.height);
}

godot::Vector2i FauxAssetSet::get_tile_size(std::int32_t picnum) const {
    if (!atlas_ || picnum < 0 || static_cast<std::uint32_t>(picnum) >= atlas_->tiles.size()) {
        return godot::Vector2i();
    }
    return godot::Vector2i(atlas_->tiles[picnum].width, atlas_->tiles[picnum].height);
}

godot::Vector2i FauxAssetSet::get_tile_pivot(std::int32_t picnum) const {
    if (!atlas_ || picnum < 0 || static_cast<std::uint32_t>(picnum) >= atlas_->tiles.size()) {
        return godot::Vector2i();
    }
    return godot::Vector2i(atlas_->tiles[picnum].x_center, atlas_->tiles[picnum].y_center);
}

godot::Dictionary FauxAssetSet::get_tile_meta(std::int32_t picnum) const {
    godot::Dictionary out;
    if (!atlas_ || picnum < 0 || static_cast<std::uint32_t>(picnum) >= atlas_->tiles.size()) {
        return out;
    }
    const auto& t = atlas_->tiles[picnum];
    out["picnum"] = picnum;
    out["populated"] = t.populated;
    // "claimed": some ART file declares a range covering this picnum. An
    // unclaimed picnum is a gap (no such tile); a claimed-but-unpopulated one
    // is a real tile with zero dimensions. M5 needs that distinction, and
    // without it the two empty states are indistinguishable at the boundary.
    out["claimed"] = !t.source.empty();
    out["page"] = t.page;
    out["width"] = t.width;
    out["height"] = t.height;
    out["x_center"] = t.x_center;
    out["y_center"] = t.y_center;
    out["frames"] = t.meta.frames;
    out["anim_type"] = t.meta.anim_type;
    out["speed"] = t.meta.speed;
    out["raw"] = t.meta.raw;
    out["source"] = godot::String(t.source.c_str());
    return out;
}

godot::Dictionary FauxAssetSet::get_stats() const {
    godot::Dictionary out;
    if (!atlas_ || !set_) {
        return out;
    }
    out["art_sources"] = static_cast<std::int64_t>(set_->arts.size());
    out["tile_count"] = static_cast<std::int64_t>(atlas_->tile_count);
    out["populated_tiles"] = static_cast<std::int64_t>(atlas_->populated_tiles);
    out["empty_gap_tiles"] = static_cast<std::int64_t>(atlas_->empty_gap_tiles);
    out["empty_zero_dim_tiles"] = static_cast<std::int64_t>(atlas_->empty_zero_dim_tiles);
    out["page_count"] = atlas_->page_count;
    out["page_size"] = godot::Vector2i(atlas_->page_width, atlas_->page_height);
    out["indexed_bytes"] = static_cast<std::int64_t>(atlas_->pixels.size());
    out["shade_rows"] = set_->palette.num_shades;
    out["alt_palettes"] = static_cast<std::int64_t>(set_->lookup.alt_palettes.size());
    out["swaps"] = static_cast<std::int64_t>(set_->lookup.swaps.size());
    return out;
}

godot::PackedByteArray FauxAssetSet::get_pixels() const {
    godot::PackedByteArray out;
    if (!atlas_) {
        return out;
    }
    out.resize(static_cast<std::int64_t>(atlas_->pixels.size()));
    if (!atlas_->pixels.empty()) {
        std::memcpy(out.ptrw(), atlas_->pixels.data(), atlas_->pixels.size());
    }
    return out;
}

godot::PackedByteArray FauxAssetSet::get_tile_indices(std::int32_t picnum) const {
    godot::PackedByteArray out;
    if (!atlas_) {
        return out;
    }
    const auto bytes = atlas_->tile_bytes(static_cast<std::uint32_t>(picnum));
    out.resize(static_cast<std::int64_t>(bytes.size()));
    if (!bytes.empty()) {
        std::memcpy(out.ptrw(), bytes.data(), bytes.size());
    }
    return out;
}

godot::Ref<godot::Image> FauxAssetSet::make_index_image(std::int32_t page) const {
    if (!atlas_ || page < 0 || page >= atlas_->page_count) {
        return nullptr;
    }
    const std::size_t page_bytes =
        static_cast<std::size_t>(atlas_->page_width) * atlas_->page_height;
    const std::size_t base = static_cast<std::size_t>(page) * page_bytes;
    godot::PackedByteArray data;
    data.resize(static_cast<std::int64_t>(page_bytes));
    std::memcpy(data.ptrw(), atlas_->pixels.data() + base, page_bytes);
    return godot::Image::create_from_data(atlas_->page_width, atlas_->page_height, false,
                                          godot::Image::FORMAT_R8, data);
}

godot::Ref<godot::Image> FauxAssetSet::make_shade_image() const {
    if (!set_) {
        return nullptr;
    }
    const auto& tables = set_->palette.shade_tables;
    if (tables.empty()) {
        return nullptr;
    }
    godot::PackedByteArray data;
    data.resize(static_cast<std::int64_t>(tables.size()));
    std::memcpy(data.ptrw(), tables.data(), tables.size());
    return godot::Image::create_from_data(256, set_->palette.num_shades, false,
                                          godot::Image::FORMAT_R8, data);
}

std::int32_t FauxAssetSet::get_shade_row_count() const {
    return set_ ? set_->palette.num_shades : 0;
}

godot::Ref<godot::Image> FauxAssetSet::make_palette_image(std::int32_t select) const {
    if (!set_ || select < 0 ||
        select > static_cast<std::int32_t>(set_->lookup.alt_palettes.size())) {
        return nullptr;
    }
    const auto& rgb = select == 0 ? set_->palette.rgb : set_->lookup.alt_palettes[select - 1];
    godot::PackedByteArray data;
    data.resize(768);
    for (std::size_t i = 0; i < 768; ++i) {
        // 6-bit VGA components scaled to full range — display conversion,
        // only ever at this presentation boundary.
        data.ptrw()[i] = static_cast<std::uint8_t>((rgb[i] * 255 + 31) / 63);
    }
    return godot::Image::create_from_data(256, 1, false, godot::Image::FORMAT_RGB8, data);
}

std::int32_t FauxAssetSet::get_palette_choice_count() const {
    return set_ ? static_cast<std::int32_t>(1 + set_->lookup.alt_palettes.size()) : 0;
}

godot::Ref<godot::Image> FauxAssetSet::make_remap_image(std::int32_t select) const {
    if (!set_ || select < 0 || select > static_cast<std::int32_t>(set_->lookup.swaps.size())) {
        return nullptr;
    }
    godot::PackedByteArray data;
    data.resize(256);
    if (select == 0) {
        for (std::int32_t c = 0; c < 256; ++c) {
            data.ptrw()[c] = static_cast<std::uint8_t>(c); // identity
        }
    } else {
        const auto& table = set_->lookup.swaps[select - 1].table;
        std::memcpy(data.ptrw(), table.data(), 256);
    }
    return godot::Image::create_from_data(256, 1, false, godot::Image::FORMAT_R8, data);
}

std::int32_t FauxAssetSet::get_remap_choice_count() const {
    return set_ ? static_cast<std::int32_t>(1 + set_->lookup.swaps.size()) : 0;
}

godot::PackedByteArray FauxAssetSet::compute_tile_rgba(std::int32_t picnum, std::int32_t shade_row,
                                                       std::int32_t palette_select,
                                                       std::int32_t remap_select) const {
    godot::PackedByteArray out;
    if (!atlas_ || !set_ || picnum < 0 ||
        static_cast<std::uint32_t>(picnum) >= atlas_->tiles.size()) {
        return out;
    }
    const auto& t = atlas_->tiles[picnum];
    if (!t.populated) {
        return out;
    }
    const auto& palette = set_->palette;
    const auto& lookup = set_->lookup;
    const auto shade = [&palette](std::int32_t row, std::uint8_t idx) -> std::uint8_t {
        if (row <= 0 || palette.shade_tables.empty()) {
            return idx;
        }
        const std::int32_t max_row =
            static_cast<std::int32_t>(palette.shade_tables.size() / 256) - 1;
        const std::int32_t r = std::min(row, max_row);
        return palette.shade_tables[static_cast<std::size_t>(r) * 256 + idx];
    };
    const auto remap = [&lookup](std::int32_t sel, std::uint8_t idx) -> std::uint8_t {
        if (sel <= 0) {
            return idx;
        }
        if (static_cast<std::size_t>(sel) > lookup.swaps.size()) {
            return idx;
        }
        return lookup.swaps[sel - 1].table[idx];
    };

    const std::uint8_t* rgb =
        palette_select <= 0
            ? palette.rgb.data()
            : (static_cast<std::size_t>(palette_select) <= lookup.alt_palettes.size()
                   ? lookup.alt_palettes[palette_select - 1].data()
                   : palette.rgb.data());

    const auto bytes = atlas_->tile_bytes(static_cast<std::uint32_t>(picnum));
    out.resize(static_cast<std::int64_t>(bytes.size()) * 4);
    std::uint8_t* w = out.ptrw();
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::uint8_t idx = remap(remap_select, bytes[i]);
        const std::uint8_t shown = shade(shade_row, idx);
        w[i * 4 + 0] = static_cast<std::uint8_t>((rgb[shown * 3 + 0] * 255 + 31) / 63);
        w[i * 4 + 1] = static_cast<std::uint8_t>((rgb[shown * 3 + 1] * 255 + 31) / 63);
        w[i * 4 + 2] = static_cast<std::uint8_t>((rgb[shown * 3 + 2] * 255 + 31) / 63);
        w[i * 4 + 3] = 255;
    }
    return out;
}

void FauxAssetSet::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("load_dir", "path", "page_width", "page_height"),
                                &FauxAssetSet::load_dir, DEFVAL(0), DEFVAL(0));
    godot::ClassDB::bind_method(godot::D_METHOD("load_grp", "path", "page_width", "page_height"),
                                &FauxAssetSet::load_grp, DEFVAL(0), DEFVAL(0));
    godot::ClassDB::bind_method(godot::D_METHOD("is_loaded"), &FauxAssetSet::is_loaded);
    godot::ClassDB::bind_method(godot::D_METHOD("get_last_error"), &FauxAssetSet::get_last_error);
    godot::ClassDB::bind_method(godot::D_METHOD("get_tile_count"), &FauxAssetSet::get_tile_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_page_size"), &FauxAssetSet::get_page_size);
    godot::ClassDB::bind_method(godot::D_METHOD("get_page_count"), &FauxAssetSet::get_page_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_tile_rect", "picnum"),
                                &FauxAssetSet::get_tile_rect);
    godot::ClassDB::bind_method(godot::D_METHOD("get_tile_size", "picnum"),
                                &FauxAssetSet::get_tile_size);
    godot::ClassDB::bind_method(godot::D_METHOD("get_tile_pivot", "picnum"),
                                &FauxAssetSet::get_tile_pivot);
    godot::ClassDB::bind_method(godot::D_METHOD("get_tile_meta", "picnum"),
                                &FauxAssetSet::get_tile_meta);
    godot::ClassDB::bind_method(godot::D_METHOD("get_stats"), &FauxAssetSet::get_stats);
    godot::ClassDB::bind_method(godot::D_METHOD("get_pixels"), &FauxAssetSet::get_pixels);
    godot::ClassDB::bind_method(godot::D_METHOD("get_tile_indices", "picnum"),
                                &FauxAssetSet::get_tile_indices);
    godot::ClassDB::bind_method(godot::D_METHOD("make_index_image", "page"),
                                &FauxAssetSet::make_index_image);
    godot::ClassDB::bind_method(godot::D_METHOD("make_shade_image"),
                                &FauxAssetSet::make_shade_image);
    godot::ClassDB::bind_method(godot::D_METHOD("get_shade_row_count"),
                                &FauxAssetSet::get_shade_row_count);
    godot::ClassDB::bind_method(godot::D_METHOD("make_palette_image", "select"),
                                &FauxAssetSet::make_palette_image);
    godot::ClassDB::bind_method(godot::D_METHOD("get_palette_choice_count"),
                                &FauxAssetSet::get_palette_choice_count);
    godot::ClassDB::bind_method(godot::D_METHOD("make_remap_image", "select"),
                                &FauxAssetSet::make_remap_image);
    godot::ClassDB::bind_method(godot::D_METHOD("get_remap_choice_count"),
                                &FauxAssetSet::get_remap_choice_count);
    godot::ClassDB::bind_method(godot::D_METHOD("compute_tile_rgba", "picnum", "shade_row",
                                                "palette_select", "remap_select"),
                                &FauxAssetSet::compute_tile_rgba);
}

} // namespace fauxbuild_godot
