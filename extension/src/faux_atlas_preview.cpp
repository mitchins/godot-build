#include "fauxbuild_godot/faux_atlas_preview.hpp"

#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace fauxbuild_godot {

namespace {

// Nearest sampling by construction: texelFetch at exact page coordinates
// computed from the tile rect. The palette texture is the ONLY place a
// colour exists; the atlas is consumed as pure indices.
const char* kPreviewShader = R"GD_SHADER(
shader_type canvas_item;
uniform sampler2D index_atlas; // R8, one palette index per texel
uniform sampler2D shade_tex;   // R8, rows x 256
uniform sampler2D remap_tex;   // R8, 256 x 1 (identity or lookup swap)
uniform sampler2D palette_tex; // RGB8, 256 x 1
uniform int shade_row = 0;
uniform int shade_rows = 32;
uniform ivec4 tile_rect = ivec4(0, 0, 1, 1); // x, y, w, h in page pixels

int fetch_index(sampler2D tex, ivec2 px) {
    return int(texelFetch(tex, px, 0).r * 255.0 + 0.5);
}

void fragment() {
    vec2 in_tile = UV * vec2(tile_rect.zw);
    ivec2 page_px = tile_rect.xy + ivec2(in_tile);
    int idx = fetch_index(index_atlas, page_px);
    int remapped = fetch_index(remap_tex, ivec2(idx, 0));
    int shaded = fetch_index(shade_tex, ivec2(remapped, clamp(shade_row, 0, shade_rows - 1)));
    COLOR = vec4(texelFetch(palette_tex, ivec2(shaded, 0), 0).rgb, 1.0);
}
)GD_SHADER";

} // namespace

void FauxAtlasPreview::set_asset(const godot::Ref<FauxAssetSet>& asset) {
    asset_ = asset;
}

godot::Ref<FauxAssetSet> FauxAtlasPreview::get_asset() const {
    return asset_;
}

void FauxAtlasPreview::_ready() {
    if (asset_.is_null() || !asset_->is_loaded()) {
        godot::UtilityFunctions::push_error("FauxAtlasPreview: no loaded FauxAssetSet assigned");
        return;
    }
    rebuild();
}

void FauxAtlasPreview::rebuild() {
    const auto stats = asset_->get_stats();
    const std::int32_t tiles = asset_->get_tile_count();

    auto* root = memnew(godot::VBoxContainer);
    root->set_anchors_and_offsets_preset(godot::Control::PRESET_FULL_RECT);
    add_child(root);

    canvas_ = memnew(godot::ColorRect);
    canvas_->set_custom_minimum_size(godot::Vector2(320, 240));
    auto shader = memnew(godot::Shader);
    shader->set_code(kPreviewShader);
    material_ = memnew(godot::ShaderMaterial);
    material_->set_shader(shader);

    // Whole-page textures handed to the GPU as single-channel data; the
    // atlas never leaves indexed form. The selected page's texture is
    // (re)bound in refresh_tile; page 0 starts bound (CodeRabbit PR#4).
    material_->set_shader_parameter(
        "index_atlas", godot::ImageTexture::create_from_image(asset_->make_index_image(0)));
    bound_page_ = 0;
    material_->set_shader_parameter(
        "shade_tex", godot::ImageTexture::create_from_image(asset_->make_shade_image()));
    material_->set_shader_parameter("shade_rows", asset_->get_shade_row_count());
    canvas_->set_material(material_);
    root->add_child(canvas_);

    auto* row = memnew(godot::HBoxContainer);
    root->add_child(row);

    auto* pic_label = memnew(godot::Label);
    pic_label->set_text("picnum");
    row->add_child(pic_label);
    picnum_box_ = memnew(godot::SpinBox);
    picnum_box_->set_min(0);
    picnum_box_->set_max(tiles - 1);
    picnum_box_->set_value(1);
    row->add_child(picnum_box_);

    auto* shade_label = memnew(godot::Label);
    shade_label->set_text("shade");
    row->add_child(shade_label);
    shade_box_ = memnew(godot::SpinBox);
    shade_box_->set_min(0);
    shade_box_->set_max(std::max(0, asset_->get_shade_row_count() - 1));
    row->add_child(shade_box_);

    auto* pal_label = memnew(godot::Label);
    pal_label->set_text("palette");
    row->add_child(pal_label);
    palette_button_ = memnew(godot::OptionButton);
    palette_button_->add_item("base");
    for (std::int32_t i = 0; i < asset_->get_palette_choice_count() - 1; ++i) {
        palette_button_->add_item(("alt " + godot::String::num(i, 0)).utf8().get_data());
    }
    for (std::int32_t i = 0; i < asset_->get_remap_choice_count() - 1; ++i) {
        palette_button_->add_item(("swap " + godot::String::num(i, 0)).utf8().get_data());
    }
    palette_button_->select(0);
    row->add_child(palette_button_);

    picnum_box_->connect("value_changed", callable_mp(this, &FauxAtlasPreview::refresh_tile));
    shade_box_->connect("value_changed", callable_mp(this, &FauxAtlasPreview::refresh_tile));
    palette_button_->connect("item_selected",
                             callable_mp(this, &FauxAtlasPreview::refresh_palette));

    refresh_palette(0); // installs base palette + identity remap textures
    refresh_tile(0);
    ready_ = true;
    godot::UtilityFunctions::print("FauxAtlasPreview: ", tiles, " picnums, ",
                                   static_cast<std::int64_t>(stats["populated_tiles"]),
                                   " populated, ", asset_->get_page_count(), " indexed page(s)");
}

void FauxAtlasPreview::refresh_tile(double value) {
    (void)value;
    if (material_.is_null() || asset_.is_null()) {
        return;
    }
    const std::int32_t picnum =
        picnum_box_ ? static_cast<std::int32_t>(picnum_box_->get_value()) : 0;
    const auto rect = asset_->get_tile_rect(picnum);
    const auto meta = asset_->get_tile_meta(picnum);
    // Multi-page atlases: rebind the index texture when the selected tile
    // lives on a different page (the rect is page-relative; CodeRabbit
    // PR#4 caught the page-0-only binding).
    const std::int32_t page = static_cast<std::int32_t>(meta["page"]);
    if (page != bound_page_) {
        material_->set_shader_parameter(
            "index_atlas", godot::ImageTexture::create_from_image(asset_->make_index_image(page)));
        bound_page_ = page;
    }
    material_->set_shader_parameter(
        "tile_rect", godot::Vector4i(rect.position.x, rect.position.y, rect.size.x, rect.size.y));
    if (static_cast<bool>(meta["populated"]) && canvas_ != nullptr) {
        // Scale the drawing rect to the tile's aspect so texelFetch UVs
        // stay aligned with page pixels.
        canvas_->set_custom_minimum_size(godot::Vector2(rect.size.x * 8, rect.size.y * 8));
    }
    if (shade_box_) {
        material_->set_shader_parameter("shade_row",
                                        static_cast<std::int32_t>(shade_box_->get_value()));
    }
}

void FauxAtlasPreview::refresh_palette(int64_t index) {
    (void)index;
    if (material_.is_null() || asset_.is_null() || palette_button_ == nullptr) {
        return;
    }
    const std::int32_t sel = palette_button_->get_selected();
    const std::int32_t alts = asset_->get_palette_choice_count() - 1;
    if (sel == 0) {
        material_->set_shader_parameter(
            "palette_tex", godot::ImageTexture::create_from_image(asset_->make_palette_image(0)));
        material_->set_shader_parameter(
            "remap_tex", godot::ImageTexture::create_from_image(asset_->make_remap_image(0)));
    } else if (sel <= alts) {
        material_->set_shader_parameter(
            "palette_tex", godot::ImageTexture::create_from_image(asset_->make_palette_image(sel)));
        material_->set_shader_parameter(
            "remap_tex", godot::ImageTexture::create_from_image(asset_->make_remap_image(0)));
    } else {
        material_->set_shader_parameter(
            "palette_tex", godot::ImageTexture::create_from_image(asset_->make_palette_image(0)));
        material_->set_shader_parameter("remap_tex", godot::ImageTexture::create_from_image(
                                                         asset_->make_remap_image(sel - alts)));
    }
    refresh_tile(0);
}

void FauxAtlasPreview::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_asset", "asset"),
                                &FauxAtlasPreview::set_asset);
    godot::ClassDB::bind_method(godot::D_METHOD("get_asset"), &FauxAtlasPreview::get_asset);
    godot::ClassDB::bind_method(godot::D_METHOD("is_ready"), &FauxAtlasPreview::is_ready);
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::OBJECT, "asset",
                                     godot::PROPERTY_HINT_NODE_TYPE, "FauxAssetSet"),
                 "set_asset", "get_asset");
}

} // namespace fauxbuild_godot
