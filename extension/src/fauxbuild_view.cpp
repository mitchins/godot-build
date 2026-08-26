#include "fauxbuild_godot/fauxbuild_view.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector4.hpp>

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

namespace {

// The indexed fragment path. The R8 atlas page is authoritative: one palette
// index per texel, sampled with NEAREST and never filtered, then looked up in
// a 256x1 base-palette texture. No RGBA form is authoritative anywhere.
//
// atlas_page carries DATA, not colour, so it must NOT be `source_color`:
// that hint asks the renderer to treat the sampled value as sRGB and apply a
// transfer curve, which would silently corrupt palette indices into
// almost-right ones -- the worst failure mode, because it still looks like a
// texture. palette_lut IS colour and keeps the hint. A gate pins the split.
//
// UVs arrive TILE-LOCAL, so fract() wraps inside the tile and the result is
// mapped into the tile's rect within the page. Wrapping across the whole page
// would bleed neighbouring tiles into each other -- which is why the rect is a
// per-group uniform rather than baked into the UVs.
constexpr const char* kIndexedShader = R"(shader_type spatial;
render_mode unshaded, cull_disabled;

uniform sampler2D atlas_page : filter_nearest, repeat_disable;
uniform sampler2D palette_lut : source_color, filter_nearest, repeat_disable;
uniform vec4 tile_rect = vec4(0.0, 0.0, 1.0, 1.0);

void fragment() {
    vec2 local = fract(UV);
    vec2 page_uv = tile_rect.xy + local * tile_rect.zw;
    float index = texture(atlas_page, page_uv).r;
    vec3 rgb = texture(palette_lut, vec2(index * (255.0 / 256.0) + (0.5 / 256.0), 0.5)).rgb;
    ALBEDO = rgb;
}
)";

godot::Ref<godot::ImageTexture> make_palette_texture(const std::vector<std::uint8_t>& rgb) {
    godot::PackedByteArray bytes;
    bytes.resize(256 * 3);
    for (int i = 0; i < 256 * 3; ++i) {
        bytes.set(i, i < static_cast<int>(rgb.size()) ? rgb[static_cast<std::size_t>(i)] : 0);
    }
    const godot::Ref<godot::Image> image =
        godot::Image::create_from_data(256, 1, false, godot::Image::FORMAT_RGB8, bytes);
    return godot::ImageTexture::create_from_image(image);
}

godot::Ref<godot::ImageTexture> make_page_texture(const std::vector<std::uint8_t>& pixels,
                                                  std::int32_t page, std::int32_t width,
                                                  std::int32_t height) {
    const std::size_t stride = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t offset = static_cast<std::size_t>(page) * stride;
    godot::PackedByteArray bytes;
    bytes.resize(static_cast<std::int64_t>(stride));
    for (std::size_t i = 0; i < stride; ++i) {
        const std::size_t src = offset + i;
        bytes.set(static_cast<std::int64_t>(i), src < pixels.size() ? pixels[src] : 0);
    }
    // FORMAT_R8: the indexed page is uploaded as-is. Nothing converts it to
    // RGBA on the way in.
    const godot::Ref<godot::Image> image =
        godot::Image::create_from_data(width, height, false, godot::Image::FORMAT_R8, bytes);
    return godot::ImageTexture::create_from_image(image);
}

} // namespace

bool FauxBuildView::present_prepared_world(const fauxbuild::PreparedWorld& prepared) {
    if (prepared.page_width <= 0 || prepared.page_height <= 0 || prepared.page_count <= 0) {
        godot::UtilityFunctions::push_error("FauxBuildView: prepared world has no atlas pages");
        return false;
    }
    for (const auto& surface : prepared.surfaces) {
        if (surface.uvs.size() != surface.vertices.size()) {
            godot::UtilityFunctions::push_error(
                "FauxBuildView: prepared surface has one UV per vertex violated");
            return false;
        }
        // The page index must be validated HERE, in the initial pass, not at
        // the point of use: pages[] is indexed after discard_presentation(),
        // so an out-of-range page would take the previous presentation down
        // with it and then read out of bounds. Rejecting up front keeps the
        // seam transactional.
        if (surface.page < 0 || surface.page >= prepared.page_count) {
            godot::UtilityFunctions::push_error(
                "FauxBuildView: prepared surface names atlas page " + godot::itos(surface.page) +
                ", outside [0, " + godot::itos(prepared.page_count) + ")");
            return false;
        }
    }

    // Build every group before touching the scene: a failure must leave the
    // previous presentation exactly as it was.
    struct Group {
        std::int32_t picnum = 0;
        std::int32_t page = 0;
        godot::Vector4 rect;
        const char* kind = "";
        godot::PackedVector3Array vertices;
        godot::PackedVector2Array uvs;
        godot::PackedInt32Array indices;
    };
    std::vector<Group> groups;
    auto group_for = [&](const fauxbuild::PreparedSurface& surface) -> Group& {
        for (Group& g : groups) {
            if (g.picnum == surface.picnum && g.kind == kGroups[kind_index(surface.kind)].name) {
                return g;
            }
        }
        Group g;
        g.picnum = surface.picnum;
        g.page = surface.page;
        g.rect = godot::Vector4(surface.rect_x, surface.rect_y, surface.rect_w, surface.rect_h);
        g.kind = kGroups[kind_index(surface.kind)].name;
        groups.push_back(std::move(g));
        return groups.back();
    };

    for (const auto& surface : prepared.surfaces) {
        Group& g = group_for(surface);
        const auto base = static_cast<std::int64_t>(g.vertices.size());
        // Same checked packing as present_world: a source index must address
        // its OWN surface's vertices, and base + index must be validated
        // before narrowing to int32. Checked here, while nothing in the scene
        // has been touched, so a rejection leaves the previous presentation
        // exactly as it was.
        const auto source_vertices = static_cast<std::int64_t>(surface.vertices.size());
        if (base + source_vertices > kMaxMeshIndex) {
            godot::UtilityFunctions::push_error(
                "FauxBuildView: accumulated vertex count for a prepared group exceeds Godot's "
                "index representation");
            return false;
        }
        for (const std::uint32_t index : surface.indices) {
            const std::int64_t global = base + static_cast<std::int64_t>(index);
            if (index >= surface.vertices.size() || global > kMaxMeshIndex) {
                godot::UtilityFunctions::push_error(
                    "FauxBuildView: prepared surface index does not address its own vertices, "
                    "or overflows Godot's index representation");
                return false;
            }
        }
        for (std::size_t i = 0; i < surface.vertices.size(); ++i) {
            const auto& v = surface.vertices[i];
            g.vertices.push_back(godot::Vector3(static_cast<float>(v.x), static_cast<float>(v.y),
                                                static_cast<float>(v.z)));
            // Verbatim: the prepared UV is uploaded unchanged.
            g.uvs.push_back(godot::Vector2(surface.uvs[i].u, surface.uvs[i].v));
        }
        for (const std::uint32_t index : surface.indices) {
            g.indices.push_back(static_cast<std::int32_t>(base + static_cast<std::int64_t>(index)));
        }
    }

    const godot::Ref<godot::ImageTexture> palette = make_palette_texture(prepared.palette_rgb);

    // One texture per PAGE, not per group: E1L1 is 173 groups over 3 pages,
    // and uploading a full page per group would be 173 copies of the same
    // megabytes. Likewise one Shader, shared -- only tile_rect differs per
    // group, and that is a material parameter.
    std::vector<godot::Ref<godot::ImageTexture>> pages;
    pages.reserve(static_cast<std::size_t>(prepared.page_count));
    for (std::int32_t page = 0; page < prepared.page_count; ++page) {
        pages.push_back(make_page_texture(prepared.atlas_pixels, page, prepared.page_width,
                                          prepared.page_height));
    }
    godot::Ref<godot::Shader> shader;
    shader.instantiate();
    shader->set_code(kIndexedShader);

    discard_presentation();
    int ordinal = 0;
    for (const Group& g : groups) {
        if (g.vertices.size() == 0) {
            continue;
        }
        godot::Ref<godot::ArrayMesh> mesh;
        mesh.instantiate();
        godot::Array arrays;
        arrays.resize(godot::Mesh::ARRAY_MAX);
        arrays[godot::Mesh::ARRAY_VERTEX] = g.vertices;
        arrays[godot::Mesh::ARRAY_TEX_UV] = g.uvs;
        arrays[godot::Mesh::ARRAY_INDEX] = g.indices;
        mesh->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, arrays);

        godot::Ref<godot::ShaderMaterial> material;
        material.instantiate();
        material->set_shader(shader);
        material->set_shader_parameter("atlas_page", pages[static_cast<std::size_t>(g.page)]);
        material->set_shader_parameter("palette_lut", palette);
        material->set_shader_parameter("tile_rect", g.rect);

        auto* instance = memnew(godot::MeshInstance3D);
        instance->set_name(godot::String(g.kind) + "_" + godot::itos(g.picnum) + "_" +
                           godot::itos(ordinal++));
        instance->set_mesh(mesh);
        instance->set_material_override(material);
        add_child(instance);
        group_ids_.push_back(godot::ObjectID(instance->get_instance_id()));
    }

    has_world_ = true;
    return true;
}

} // namespace fauxbuild_godot
