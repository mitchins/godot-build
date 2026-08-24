#pragma once

#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <memory>

#include "fauxbuild_godot/faux_asset_set.hpp"

namespace fauxbuild_godot {

// Intentionally boring debug preview (M4 slice 4): proves the palette path
// R8/index texture + palette texture + shader with nearest (texel-exact)
// sampling. Selectable picnum, shade row, palette (base/alt), and lookup
// remap. No PBR, no lighting, no world geometry — nothing here is allowed
// to grow into M5. The composition order remap -> shade -> palette is a
// debug-tool choice, not a claimed game semantic.
class FauxAtlasPreview : public godot::Control {
    GDCLASS(FauxAtlasPreview, godot::Control)

  protected:
    static void _bind_methods();

  public:
    void set_asset(const godot::Ref<FauxAssetSet>& asset);
    godot::Ref<FauxAssetSet> get_asset() const;

    void _ready() override;
    bool is_ready() const { return ready_; }

  private:
    void rebuild();
    void refresh_tile(double value);
    void refresh_palette(int64_t index);

    godot::Ref<FauxAssetSet> asset_;
    godot::Ref<godot::ShaderMaterial> material_;
    godot::SpinBox* picnum_box_ = nullptr;
    godot::SpinBox* shade_box_ = nullptr;
    godot::OptionButton* palette_button_ = nullptr;
    godot::ColorRect* canvas_ = nullptr;
    bool ready_ = false;
};

} // namespace fauxbuild_godot
