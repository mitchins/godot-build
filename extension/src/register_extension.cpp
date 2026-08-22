#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include "fauxbuild_godot/fauxbuild_runtime.hpp"
#include "fauxbuild_godot/fauxbuild_view.hpp"

namespace {

void initialize_fauxbuild_extension(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    godot::ClassDB::register_class<fauxbuild_godot::FauxBuildRuntime>();
    godot::ClassDB::register_class<fauxbuild_godot::FauxBuildView>();
}

void uninitialize_fauxbuild_extension(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

} // namespace

extern "C" {

GDExtensionBool GDE_EXPORT fauxbuild_library_init(
    GDExtensionInterfaceGetProcAddress get_proc_address, const GDExtensionClassLibraryPtr library,
    GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject init_obj(get_proc_address, library, initialization);
    init_obj.register_initializer(initialize_fauxbuild_extension);
    init_obj.register_terminator(uninitialize_fauxbuild_extension);
    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}

} // extern "C"
