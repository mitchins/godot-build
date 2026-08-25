#pragma once

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <vector>

#include "fauxbuild/structural.hpp"

namespace fauxbuild_godot {

// Presents the FauxBuild world through Godot (plan §11.1).
//
// M5 slice 2: disposable structural presentation. The input is a core
// StructuralWorld — accepted geometry in render space, nothing else. This
// node is a copier/packer: it groups the world's surfaces into at most five
// diagnostic ArrayMesh groups (one MeshInstance3D per non-empty SurfaceKind)
// and never re-derives, reorders, welds, or transforms geometry. Vertices
// arrive already converted by the one core conversion; the only numeric work
// here is packaging doubles into Godot's float32 Vector3 (presentation
// narrowing, not a transform).
//
// Everything generated is disposable: presenting a world tears down and
// deterministically recreates the whole presentation, and no generated mesh,
// scene, or cache may become world authority (RENDERING_CONTRACT, D0016).
class FauxBuildView : public godot::Node3D {
    GDCLASS(FauxBuildView, godot::Node3D)

  protected:
    static void _bind_methods();

  public:
    // The production seam (M5 slice 2). C++-only and deliberately NOT bound
    // to ClassDB: a StructuralWorld cannot travel through GDScript, so the
    // only callers are C++ owners of a world (today the fixture/sample
    // harness; later the production map loader). This node never parses,
    // loads, or derives a world itself. Returns false (leaving any previous
    // presentation untouched) if the world violates the packing invariants.
    bool present_world(const fauxbuild::StructuralWorld& world);

    // Presentation-only state (diagnostic; never world state).
    bool has_world() const { return has_world_; }
    // Names of the non-empty kind groups in canonical kind order.
    godot::PackedStringArray get_group_names() const;

  private:
    void discard_presentation();

    // Stable Godot object identities, not raw pointers. A generated group can
    // be freed externally at any time; a raw MeshInstance3D* left behind is
    // dereferenced by get_group_names() and by the next discard_presentation()
    // (reproduced: SIGSEGV after queue_free + a frame boundary). ObjectID
    // survives the object's death and can be validated before every access.
    // ObjectDB use stays confined to extension/ -- core gains no dependency.
    std::vector<godot::ObjectID> group_ids_;

    // Resolve a tracked id to a live node, or nullptr if it has been freed or
    // is no longer ours. Never returns a dangling pointer.
    static godot::MeshInstance3D* resolve_group(godot::ObjectID id);
    bool has_world_ = false;
};

} // namespace fauxbuild_godot
