#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/rect2i.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <memory>

#include "fauxbuild/asset_set.hpp"
#include "fauxbuild/atlas.hpp"

namespace fauxbuild_godot {

// Consumer boundary of the indexed atlas (M4 slice 4). Wraps a core
// AssetSet + IndexedAtlas and hands the AUTHORITATIVE indexed payload to
// Godot as plain bytes plus rect/metadata dictionaries. RGBA exists only
// behind explicitly named derived helpers (images, compute_tile_rgba) that
// re-derive from palette data on every call and are never stored.
class FauxAssetSet : public godot::RefCounted {
    GDCLASS(FauxAssetSet, godot::RefCounted)

  protected:
    static void _bind_methods();

  public:
    // Loading (exactly one of these succeeds; both mount through the core
    // VFS — the GRP path is the production route, no extraction).
    bool load_dir(const godot::String& path, int page_width = 0, int page_height = 0);
    bool load_grp(const godot::String& path, int page_width = 0, int page_height = 0);
    bool is_loaded() const;
    godot::String get_last_error() const;

    // Atlas metadata.
    std::int32_t get_tile_count() const;
    godot::Vector2i get_page_size() const;
    std::int32_t get_page_count() const;
    godot::Rect2i get_tile_rect(std::int32_t picnum) const;
    godot::Vector2i get_tile_size(std::int32_t picnum) const;
    godot::Vector2i get_tile_pivot(std::int32_t picnum) const;
    godot::Dictionary get_tile_meta(std::int32_t picnum) const;
    godot::Dictionary get_stats() const;

    // Authoritative indexed texels as Godot receives them.
    godot::PackedByteArray get_pixels() const;
    godot::PackedByteArray get_tile_indices(std::int32_t picnum) const;

    // Derived, disposable previews (never stored, never source of truth).
    godot::Ref<godot::Image> make_index_image(std::int32_t page) const; // FORMAT_R8
    godot::Ref<godot::Image> make_shade_image() const;                  // FORMAT_R8
    std::int32_t get_shade_row_count() const;
    godot::Ref<godot::Image> make_palette_image(std::int32_t select) const; // RGB8
    std::int32_t get_palette_choice_count() const;                          // 1 base + alt palettes
    godot::Ref<godot::Image> make_remap_image(std::int32_t select) const;   // FORMAT_R8
    std::int32_t get_remap_choice_count() const;                            // 1 identity + swaps
    // Full preview math for one tile: index -> remap -> shade row ->
    // palette, 4 bytes RGBA per texel. Derived on every call.
    godot::PackedByteArray compute_tile_rgba(std::int32_t picnum, std::int32_t shade_row,
                                             std::int32_t palette_select,
                                             std::int32_t remap_select) const;

    const fauxbuild::AssetSet* core_set() const { return set_.get(); }
    const fauxbuild::IndexedAtlas* core_atlas() const { return atlas_.get(); }

  private:
    std::shared_ptr<fauxbuild::AssetSet> set_;
    std::shared_ptr<fauxbuild::IndexedAtlas> atlas_;
    godot::String last_error_;
};

} // namespace fauxbuild_godot
