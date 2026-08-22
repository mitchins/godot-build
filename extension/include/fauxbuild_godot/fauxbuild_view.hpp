#pragma once

#include <godot_cpp/classes/node3d.hpp>

namespace fauxbuild_godot {

// Presents the FauxBuild world through Godot (plan §11.1).
// M1 scope: registration only. Camera adaptation, GPU resources, geometry
// caching, and interpolation arrive with M5 and later milestones.
class FauxBuildView : public godot::Node3D {
    GDCLASS(FauxBuildView, godot::Node3D)

  protected:
    static void _bind_methods();
};

} // namespace fauxbuild_godot
