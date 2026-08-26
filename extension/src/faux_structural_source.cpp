#include "fauxbuild_godot/faux_structural_source.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <cstddef>
#include <string_view>
#include <utility>

#include "fauxbuild/asset_set.hpp"
#include "fauxbuild/atlas.hpp"
#include "fauxbuild/map_io.hpp"
#include "fauxbuild/prepared.hpp"
#include "fauxbuild/structural.hpp"
#include "fauxbuild/vfs.hpp"
#include "fauxbuild_godot/fauxbuild_view.hpp"

namespace fauxbuild_godot {

namespace {

// Stage-tagged structured error so a caller can see which link of the
// production chain failed without any parsing of free-form text.
godot::String stage_error(const char* stage, const fauxbuild::ParseError& error) {
    return godot::String(stage) + ": " + error.to_string().c_str();
}

} // namespace

void FauxStructuralSource::_bind_methods() {
    using godot::ClassDB;
    ClassDB::bind_method(godot::D_METHOD("present_grp", "grp_path", "map_name", "view"),
                         &FauxStructuralSource::present_grp);
    ClassDB::bind_method(godot::D_METHOD("present_dir", "dir_path", "map_name", "view"),
                         &FauxStructuralSource::present_dir);
    ClassDB::bind_method(godot::D_METHOD("present_grp_textured", "grp_path", "map_name", "view"),
                         &FauxStructuralSource::present_grp_textured);
    ClassDB::bind_method(godot::D_METHOD("present_dir_textured", "dir_path", "map_name", "view"),
                         &FauxStructuralSource::present_dir_textured);
    ClassDB::bind_method(godot::D_METHOD("get_source_description"),
                         &FauxStructuralSource::get_source_description);
    ClassDB::bind_method(godot::D_METHOD("get_map_name"), &FauxStructuralSource::get_map_name);
    ClassDB::bind_method(godot::D_METHOD("get_sector_count"),
                         &FauxStructuralSource::get_sector_count);
    ClassDB::bind_method(godot::D_METHOD("get_wall_count"), &FauxStructuralSource::get_wall_count);
    ClassDB::bind_method(godot::D_METHOD("get_surface_count"),
                         &FauxStructuralSource::get_surface_count);
    ClassDB::bind_method(godot::D_METHOD("get_triangle_count"),
                         &FauxStructuralSource::get_triangle_count);
    ClassDB::bind_method(godot::D_METHOD("get_note_count"), &FauxStructuralSource::get_note_count);
    ClassDB::bind_method(godot::D_METHOD("get_diagnostic_count"),
                         &FauxStructuralSource::get_diagnostic_count);
    ClassDB::bind_method(godot::D_METHOD("get_start_position"),
                         &FauxStructuralSource::get_start_position);
    ClassDB::bind_method(godot::D_METHOD("get_last_error"), &FauxStructuralSource::get_last_error);
}

bool FauxStructuralSource::present_grp(const godot::String& grp_path, const godot::String& map_name,
                                       FauxBuildView* view) {
    auto mount = fauxbuild::GrpMount::create(grp_path.utf8().get_data());
    if (!mount.is_ok()) {
        last_error_ = stage_error("grp mount", mount.error());
        return false;
    }
    return present_from_mount(std::move(mount.value()), map_name, false, view);
}

bool FauxStructuralSource::present_dir(const godot::String& dir_path, const godot::String& map_name,
                                       FauxBuildView* view) {
    auto mount = fauxbuild::DirectoryMount::create(dir_path.utf8().get_data());
    if (!mount.is_ok()) {
        last_error_ = stage_error("directory mount", mount.error());
        return false;
    }
    return present_from_mount(std::move(mount.value()), map_name, false, view);
}

bool FauxStructuralSource::present_grp_textured(const godot::String& grp_path,
                                                const godot::String& map_name,
                                                FauxBuildView* view) {
    auto mount = fauxbuild::GrpMount::create(grp_path.utf8().get_data());
    if (!mount.is_ok()) {
        last_error_ = stage_error("grp mount", mount.error());
        return false;
    }
    return present_from_mount(std::move(mount.value()), map_name, true, view);
}

bool FauxStructuralSource::present_dir_textured(const godot::String& dir_path,
                                                const godot::String& map_name,
                                                FauxBuildView* view) {
    auto mount = fauxbuild::DirectoryMount::create(dir_path.utf8().get_data());
    if (!mount.is_ok()) {
        last_error_ = stage_error("directory mount", mount.error());
        return false;
    }
    return present_from_mount(std::move(mount.value()), map_name, true, view);
}

bool FauxStructuralSource::present_from_mount(std::unique_ptr<fauxbuild::Mount> mount,
                                              const godot::String& map_name, bool textured,
                                              FauxBuildView* view) {
    if (view == nullptr) {
        last_error_ = "no FauxBuildView given";
        return false;
    }

    // Derive the complete new world FIRST; the view is only handed a world
    // after every stage below has succeeded, so a failure anywhere leaves
    // the previously presented world and the reporting facts untouched.
    fauxbuild::Vfs vfs;
    const godot::String description = mount->describe().c_str();
    vfs.add_mount(std::move(mount));

    auto file = vfs.open(map_name.utf8().get_data());
    if (!file.is_ok()) {
        last_error_ = stage_error("vfs lookup", file.error());
        return false;
    }

    const auto& bytes = file.value().bytes;
    const std::string_view view_bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    auto map = fauxbuild::read_map(view_bytes, file.value().origin);
    if (!map.is_ok()) {
        last_error_ = stage_error("map parse", map.error());
        return false;
    }

    auto world = fauxbuild::build_structural_world(map.value());
    if (!world.is_ok()) {
        last_error_ = stage_error("structural derivation", world.error());
        return false;
    }

    if (textured) {
        // Assets come from the SAME Vfs the map was read through.
        auto assets = fauxbuild::load_asset_set(vfs);
        if (!assets.is_ok()) {
            last_error_ = stage_error("asset load", assets.error());
            return false;
        }
        auto atlas = fauxbuild::build_indexed_atlas(assets.value().arts, {});
        if (!atlas.is_ok()) {
            last_error_ = stage_error("atlas", atlas.error());
            return false;
        }
        auto prepared =
            fauxbuild::prepare_world(world.value(), atlas.value(), assets.value().palette);
        if (!prepared.is_ok()) {
            last_error_ = stage_error("prepare", prepared.error());
            return false;
        }
        if (!view->present_prepared_world(prepared.value())) {
            last_error_ = "view rejected the prepared world (see errors above)";
            return false;
        }
    } else if (!view->present_world(world.value())) {
        last_error_ = "view rejected the world (see errors above)";
        return false;
    }

    // Commit reporting facts only now, after a fully successful chain.
    std::size_t triangles = 0;
    for (const auto& surface : world.value().surfaces) {
        triangles += surface.indices.size() / 3;
    }

    // Start position through THE transform (D0016) -- never a second one.
    const auto& start = map.value().start;
    const fauxbuild::StructuralVertex start_render =
        fauxbuild::to_render_space(start.x, start.y, start.z);
    start_position_ =
        godot::Vector3(static_cast<float>(start_render.x), static_cast<float>(start_render.y),
                       static_cast<float>(start_render.z));

    source_description_ = description;
    map_name_ = file.value().name.c_str();
    sector_count_ = static_cast<std::int32_t>(map.value().sectors.size());
    wall_count_ = static_cast<std::int32_t>(map.value().walls.size());
    surface_count_ = static_cast<std::int32_t>(world.value().surfaces.size());
    triangle_count_ = static_cast<std::int32_t>(triangles);
    note_count_ = static_cast<std::int32_t>(world.value().notes.size());
    diagnostic_count_ = static_cast<std::int32_t>(world.value().diagnostics.size());
    last_error_ = "";
    return true;
}

godot::String FauxStructuralSource::get_source_description() const {
    return source_description_;
}

godot::String FauxStructuralSource::get_map_name() const {
    return map_name_;
}

std::int32_t FauxStructuralSource::get_sector_count() const {
    return sector_count_;
}

std::int32_t FauxStructuralSource::get_wall_count() const {
    return wall_count_;
}

std::int32_t FauxStructuralSource::get_surface_count() const {
    return surface_count_;
}

std::int32_t FauxStructuralSource::get_triangle_count() const {
    return triangle_count_;
}

std::int32_t FauxStructuralSource::get_note_count() const {
    return note_count_;
}

std::int32_t FauxStructuralSource::get_diagnostic_count() const {
    return diagnostic_count_;
}

godot::Vector3 FauxStructuralSource::get_start_position() const {
    return start_position_;
}

godot::String FauxStructuralSource::get_last_error() const {
    return last_error_;
}

} // namespace fauxbuild_godot
