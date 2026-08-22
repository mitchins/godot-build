#pragma once

#include <godot_cpp/classes/node.hpp>

namespace fauxbuild_godot {

// Hosts the FauxBuild world runtime inside a Godot scene (plan §11.1).
// M1 scope: registration and a core-version probe. Mounting, world ownership,
// ticking, and commands arrive with later milestones.
class FauxBuildRuntime : public godot::Node {
    GDCLASS(FauxBuildRuntime, godot::Node)

  protected:
    static void _bind_methods();

  public:
    godot::String get_core_version() const;
};

} // namespace fauxbuild_godot
