#include "fauxbuild_godot/fauxbuild_runtime.hpp"

#include <godot_cpp/core/class_db.hpp>

#include "fauxbuild/version.hpp"

namespace fauxbuild_godot {

godot::String FauxBuildRuntime::get_core_version() const {
    return godot::String(fauxbuild::version_string());
}

void FauxBuildRuntime::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("get_core_version"),
                                &FauxBuildRuntime::get_core_version);
}

} // namespace fauxbuild_godot
