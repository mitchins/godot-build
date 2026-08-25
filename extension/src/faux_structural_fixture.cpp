#include "fauxbuild_godot/faux_structural_fixture.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include "fauxbuild/map_synth.hpp"
#include "fauxbuild_godot/fauxbuild_view.hpp"

namespace fauxbuild_godot {

void FauxStructuralFixture::_bind_methods() {
    using godot::ClassDB;
    ClassDB::bind_method(godot::D_METHOD("get_fixture_names"),
                         &FauxStructuralFixture::get_fixture_names);
    ClassDB::bind_method(godot::D_METHOD("present", "name", "view"),
                         &FauxStructuralFixture::present);
    ClassDB::bind_method(godot::D_METHOD("write_fixture_map", "name", "directory"),
                         &FauxStructuralFixture::write_fixture_map);
    ClassDB::bind_method(godot::D_METHOD("get_last_error"), &FauxStructuralFixture::get_last_error);
    ClassDB::bind_method(godot::D_METHOD("expected_surface_count", "kind"),
                         &FauxStructuralFixture::expected_surface_count);
    ClassDB::bind_method(godot::D_METHOD("expected_vertices", "kind"),
                         &FauxStructuralFixture::expected_vertices);
    ClassDB::bind_method(godot::D_METHOD("expected_indices", "kind"),
                         &FauxStructuralFixture::expected_indices);
    ClassDB::bind_method(godot::D_METHOD("get_note_count"), &FauxStructuralFixture::get_note_count);
    ClassDB::bind_method(godot::D_METHOD("get_diagnostic_count"),
                         &FauxStructuralFixture::get_diagnostic_count);
}

godot::PackedStringArray FauxStructuralFixture::get_fixture_names() const {
    godot::PackedStringArray names;
    for (const auto& name : fauxbuild::synth::map_fixture_names()) {
        names.push_back(name.c_str());
    }
    return names;
}

godot::String FauxStructuralFixture::get_last_error() const {
    return last_error_;
}

bool FauxStructuralFixture::present(const godot::String& name, FauxBuildView* view) {
    last_error_ = "";
    if (view == nullptr) {
        last_error_ = "no FauxBuildView given";
        return false;
    }

    auto map = fauxbuild::synth::map_fixture(name.utf8().get_data());
    if (!map.is_ok()) {
        last_error_ = "fixture '" + name + "': " + map.error().to_string().c_str();
        return false;
    }

    auto world = fauxbuild::build_structural_world(map.value());
    if (!world.is_ok()) {
        last_error_ =
            "structural derivation of '" + name + "': " + world.error().to_string().c_str();
        return false;
    }

    if (!view->present_world(world.value())) {
        last_error_ = "FauxBuildView rejected the world (see errors above)";
        return false;
    }

    world_ = std::make_shared<const fauxbuild::StructuralWorld>(std::move(world.value()));
    return true;
}

namespace {

// GDScript callers pass 0..4; anything else is a caller bug and fails
// cleanly (empty result) rather than throwing across the binding boundary.
bool kind_from_int(std::int32_t kind, fauxbuild::SurfaceKind* out) {
    switch (kind) {
    case 0:
        *out = fauxbuild::SurfaceKind::Floor;
        return true;
    case 1:
        *out = fauxbuild::SurfaceKind::Ceiling;
        return true;
    case 2:
        *out = fauxbuild::SurfaceKind::SolidWall;
        return true;
    case 3:
        *out = fauxbuild::SurfaceKind::PortalUpper;
        return true;
    case 4:
        *out = fauxbuild::SurfaceKind::PortalLower;
        return true;
    default:
        return false;
    }
}

} // namespace

godot::String FauxStructuralFixture::write_fixture_map(const godot::String& name,
                                                       const godot::String& directory) {
    last_error_ = "";
    const std::string fixture = name.utf8().get_data();
    const std::string dir = directory.utf8().get_data();
    if (fixture.empty() || dir.empty()) {
        last_error_ = "write_fixture_map needs a fixture name and an output directory";
        return "";
    }

    auto bytes = fauxbuild::synth::serialize_map_fixture(fixture);
    if (!bytes.is_ok()) {
        last_error_ = "fixture '" + name + "': " + bytes.error().to_string().c_str();
        return "";
    }

    // The on-disk filename is the normalized VFS key, so the production
    // route can look the file up by the returned name through a plain
    // directory mount.
    std::string upper;
    upper.reserve(fixture.size());
    for (const char c : fixture) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    const std::string vfs_name = upper + ".MAP";
    const std::filesystem::path out = std::filesystem::path(dir) / vfs_name;

    std::ofstream stream(out, std::ios::binary | std::ios::trunc);
    if (!stream) {
        last_error_ = "cannot open '" + godot::String(out.string().c_str()) + "' for writing";
        return "";
    }
    stream.write(reinterpret_cast<const char*>(bytes.value().data()),
                 static_cast<std::streamsize>(bytes.value().size()));
    stream.close();
    if (!stream) {
        last_error_ = "failed writing '" + godot::String(out.string().c_str()) + "'";
        return "";
    }
    return vfs_name.c_str();
}

std::int32_t FauxStructuralFixture::expected_surface_count(std::int32_t kind) const {
    fauxbuild::SurfaceKind surface_kind;
    if (!world_ || !kind_from_int(kind, &surface_kind)) {
        return 0;
    }
    std::int32_t count = 0;
    for (const auto& surface : world_->surfaces) {
        if (surface.kind == surface_kind) {
            ++count;
        }
    }
    return count;
}

godot::PackedVector3Array FauxStructuralFixture::expected_vertices(std::int32_t kind) const {
    godot::PackedVector3Array out;
    fauxbuild::SurfaceKind surface_kind;
    if (!world_ || !kind_from_int(kind, &surface_kind)) {
        return out;
    }
    for (const auto& surface : world_->surfaces) {
        if (surface.kind != surface_kind) {
            continue;
        }
        for (const auto& vertex : surface.vertices) {
            out.push_back(godot::Vector3(vertex.x, vertex.y, vertex.z));
        }
    }
    return out;
}

godot::PackedInt32Array FauxStructuralFixture::expected_indices(std::int32_t kind) const {
    godot::PackedInt32Array out;
    fauxbuild::SurfaceKind surface_kind;
    if (!world_ || !kind_from_int(kind, &surface_kind)) {
        return out;
    }
    std::int64_t offset = 0;
    for (const auto& surface : world_->surfaces) {
        if (surface.kind != surface_kind) {
            continue;
        }
        for (const std::uint32_t index : surface.indices) {
            out.push_back(static_cast<std::int32_t>(offset + index));
        }
        offset += static_cast<std::int64_t>(surface.vertices.size());
    }
    return out;
}

std::int32_t FauxStructuralFixture::get_note_count() const {
    return world_ ? static_cast<std::int32_t>(world_->notes.size()) : 0;
}

std::int32_t FauxStructuralFixture::get_diagnostic_count() const {
    return world_ ? static_cast<std::int32_t>(world_->diagnostics.size()) : 0;
}

} // namespace fauxbuild_godot
