#include "fauxbuild_godot/fauxbuild_view.hpp"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace fauxbuild_godot {

namespace {

constexpr std::size_t kKindCount = 5;

std::size_t kind_index(fauxbuild::SurfaceKind kind) {
    switch (kind) {
    case fauxbuild::SurfaceKind::Floor:
        return 0;
    case fauxbuild::SurfaceKind::Ceiling:
        return 1;
    case fauxbuild::SurfaceKind::SolidWall:
        return 2;
    case fauxbuild::SurfaceKind::PortalUpper:
        return 3;
    case fauxbuild::SurfaceKind::PortalLower:
        return 4;
    }
    return 0; // unreachable; all enum values handled above
}

struct GroupSpec {
    fauxbuild::SurfaceKind kind;
    const char* name;
    godot::Color color; // diagnostic only; not a compatibility contract
};

const GroupSpec kGroups[kKindCount] = {
    {fauxbuild::SurfaceKind::Floor, "Floors", godot::Color(0.29f, 0.65f, 0.35f)},
    {fauxbuild::SurfaceKind::Ceiling, "Ceilings", godot::Color(0.35f, 0.50f, 0.80f)},
    {fauxbuild::SurfaceKind::SolidWall, "SolidWalls", godot::Color(0.75f, 0.75f, 0.75f)},
    {fauxbuild::SurfaceKind::PortalUpper, "PortalUpper", godot::Color(0.92f, 0.62f, 0.18f)},
    {fauxbuild::SurfaceKind::PortalLower, "PortalLower", godot::Color(0.82f, 0.30f, 0.24f)},
};

// Godot's mesh index arrays are int32; nothing here may narrow silently.
constexpr std::int64_t kMaxMeshIndex = std::numeric_limits<std::int32_t>::max();

} // namespace

void FauxBuildView::_bind_methods() {
    using godot::ClassDB;
    ClassDB::bind_method(godot::D_METHOD("has_world"), &FauxBuildView::has_world);
    ClassDB::bind_method(godot::D_METHOD("get_group_names"), &FauxBuildView::get_group_names);
}

godot::MeshInstance3D* FauxBuildView::resolve_group(godot::ObjectID id) {
    if (!id.is_valid()) {
        return nullptr;
    }
    godot::Object* object = godot::ObjectDB::get_instance(static_cast<uint64_t>(id));
    if (object == nullptr) {
        return nullptr; // freed externally since we generated it
    }
    return godot::Object::cast_to<godot::MeshInstance3D>(object);
}

godot::PackedStringArray FauxBuildView::get_group_names() const {
    godot::PackedStringArray names;
    for (const godot::ObjectID id : group_ids_) {
        godot::MeshInstance3D* child = resolve_group(id);
        if (child != nullptr) {
            names.push_back(child->get_name()); // dead entries are simply absent
        }
    }
    return names;
}

void FauxBuildView::discard_presentation() {
    // Remove every tracked group from the tree immediately (so fresh nodes
    // can take their names) and free them. A mesh edited, replaced, or
    // damaged externally dies with its instance here; a rebuild never reads
    // presentation state back.
    for (const godot::ObjectID id : group_ids_) {
        godot::MeshInstance3D* child = resolve_group(id);
        if (child == nullptr) {
            continue; // already freed externally; nothing to tear down
        }
        if (child->is_queued_for_deletion()) {
            // Deletion is already scheduled. Detaching is still required so a
            // fresh group can take the name this frame, but queue_free again
            // would be a double free.
            if (child->get_parent() == this) {
                remove_child(child);
            }
            continue;
        }
        if (child->get_parent() == this) {
            remove_child(child); // only detach what is still ours
        }
        child->queue_free();
    }
    group_ids_.clear();
}

bool FauxBuildView::present_world(const fauxbuild::StructuralWorld& world) {
    // Pack every kind first. Only when all packing succeeds is existing
    // presentation torn down, so an invalid world cannot leave a half-built
    // shell behind (fail cleanly, D0006: explicit checked assumptions, not
    // silent narrowing).
    godot::PackedVector3Array vertices[kKindCount];
    godot::PackedInt32Array indices[kKindCount];

    for (const auto& surface : world.surfaces) {
        const std::size_t k = kind_index(surface.kind);
        godot::PackedVector3Array& out_vertices = vertices[k];
        godot::PackedInt32Array& out_indices = indices[k];

        const std::int64_t offset = out_vertices.size();
        const std::int64_t source_vertices = static_cast<std::int64_t>(surface.vertices.size());
        if (offset + source_vertices > kMaxMeshIndex) {
            godot::UtilityFunctions::push_error(
                "FauxBuildView: accumulated vertex count for a group exceeds Godot's index "
                "representation");
            return false;
        }

        // Source indices must address the source surface's own vertices, and
        // offset + index must be checked before narrowing to int32.
        for (const std::uint32_t index : surface.indices) {
            const std::int64_t global = offset + static_cast<std::int64_t>(index);
            if (index >= surface.vertices.size() || global > kMaxMeshIndex) {
                godot::UtilityFunctions::push_error(
                    "FauxBuildView: structural surface index does not address its own "
                    "vertices, or overflows Godot's index representation");
                return false;
            }
            out_indices.push_back(static_cast<std::int32_t>(global));
        }

        // The only numeric operation on the accepted geometry: packaging the
        // core double components into Godot's float32 Vector3. No scale, no
        // sign change, no axis swap, no offset.
        for (const auto& vertex : surface.vertices) {
            out_vertices.push_back(godot::Vector3(vertex.x, vertex.y, vertex.z));
        }
    }

    discard_presentation();

    for (const GroupSpec& spec : kGroups) {
        const std::size_t k = kind_index(spec.kind);
        if (vertices[k].size() == 0) {
            continue; // empty kinds have no mesh and no node
        }

        godot::Ref<godot::ArrayMesh> mesh;
        mesh.instantiate();
        godot::Array arrays;
        arrays.resize(godot::Mesh::ARRAY_MAX);
        arrays[godot::Mesh::ARRAY_VERTEX] = vertices[k];
        arrays[godot::Mesh::ARRAY_INDEX] = indices[k];
        mesh->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, arrays);

        godot::Ref<godot::StandardMaterial3D> material;
        material.instantiate();
        material->set_shading_mode(godot::BaseMaterial3D::SHADING_MODE_UNSHADED);
        // Two-sided on purpose: slice 1 already proves winding; this viewer
        // exposes topology, and culling must not make a missing face look
        // like a geometry failure.
        material->set_cull_mode(godot::BaseMaterial3D::CULL_DISABLED);
        material->set_albedo(spec.color);

        auto* instance = memnew(godot::MeshInstance3D);
        instance->set_name(spec.name);
        instance->set_mesh(mesh);
        instance->set_material_override(material);
        add_child(instance);
        group_ids_.push_back(godot::ObjectID(instance->get_instance_id()));
    }

    has_world_ = true;
    return true;
}

} // namespace fauxbuild_godot
