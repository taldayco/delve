#include "map/map_generator.h"
#include "map/map_node.h"
#include "rng_manager.hpp"
#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_map_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
    return;

  // Register every class you want Godot to know about:
  ClassDB::register_class<RNGManager>();
  ClassDB::register_class<MapNode>();
  ClassDB::register_class<MapGenerator>();
}

void uninitialize_map_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
    return;
  // Usually nothing to uninitialize unless you allocated static memory,
  // singletons, etc.
}

extern "C" {
GDExtensionBool GDE_EXPORT
map_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                 GDExtensionClassLibraryPtr p_library,
                 GDExtensionInitialization *r_initialization) {
  GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library,
                                          r_initialization);
  init_obj.register_initializer(initialize_map_module);
  init_obj.register_terminator(uninitialize_map_module);
  init_obj.set_minimum_library_initialization_level(
      MODULE_INITIALIZATION_LEVEL_SCENE);
  return init_obj.init();
}
}
