#include "map/map_node.h"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/core/property_info.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/variant.hpp"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void MapNode::_bind_methods() {

  // Type property bind get_type_value for Godot (returns int, not enum class)
  ClassDB::bind_method(D_METHOD("set_type", "type"), &MapNode::set_type);
  ClassDB::bind_method(D_METHOD("get_type_value"), &MapNode::get_type_value);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "type", PROPERTY_HINT_ENUM,
                            "NotAssigned,Enemy,Loot,Shelter,Wenny,Boss"),
               "set_type", "get_type_value");

  // Row property
  ClassDB::bind_method(D_METHOD("set_row", "row"), &MapNode::set_row);
  ClassDB::bind_method(D_METHOD("get_row"), &MapNode::get_row);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "row"), "set_row", "get_row");

  // Column property
  ClassDB::bind_method(D_METHOD("set_column", "column"), &MapNode::set_column);
  ClassDB::bind_method(D_METHOD("get_column"), &MapNode::get_column);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "column"), "set_column",
               "get_column");

  // Position property
  ClassDB::bind_method(D_METHOD("set_position", "position"),
                       &MapNode::set_position);
  ClassDB::bind_method(D_METHOD("get_position"), &MapNode::get_position);
  ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "position"), "set_position",
               "get_position");

  // Next nodes property with type hint for TypedArray
  ClassDB::bind_method(D_METHOD("set_next_nodes", "next_nodes"),
                       &MapNode::set_next_nodes);
  ClassDB::bind_method(D_METHOD("get_next_nodes"), &MapNode::get_next_nodes);
  ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "next_nodes",
                            PROPERTY_HINT_ARRAY_TYPE,
                            vformat("%s/%s:%s", Variant::OBJECT,
                                    PROPERTY_HINT_RESOURCE_TYPE, "MapNode")),
               "set_next_nodes", "get_next_nodes");

  // Selected property
  ClassDB::bind_method(D_METHOD("set_selected", "selected"),
                       &MapNode::set_selected);
  ClassDB::bind_method(D_METHOD("is_selected"), &MapNode::is_selected);
  ADD_PROPERTY(PropertyInfo(Variant::BOOL, "selected"), "set_selected",
               "is_selected");

  // Utility methods
  ClassDB::bind_method(D_METHOD("has_next_nodes"), &MapNode::has_next_nodes);
  ClassDB::bind_method(D_METHOD("add_next_node", "node"),
                       &MapNode::add_next_node);
  ClassDB::bind_method(D_METHOD("clear_next_nodes"),
                       &MapNode::clear_next_nodes);
  ClassDB::bind_method(D_METHOD("_to_string"), &MapNode::_to_string);

  // Bind enum class constants manually (BIND_ENUM_CONSTANT doesn't work with
  // enum class)
  using enum Type;
  ClassDB::bind_integer_constant(get_class_static(), "Type", "NotAssigned",
                                 static_cast<int64_t>(NotAssigned));
  ClassDB::bind_integer_constant(get_class_static(), "Type", "Enemy",
                                 static_cast<int64_t>(Enemy));
  ClassDB::bind_integer_constant(get_class_static(), "Type", "Loot",
                                 static_cast<int64_t>(Loot));
  ClassDB::bind_integer_constant(get_class_static(), "Type", "Shelter",
                                 static_cast<int64_t>(Shelter));
  ClassDB::bind_integer_constant(get_class_static(), "Type", "Wenny",
                                 static_cast<int64_t>(Wenny));
  ClassDB::bind_integer_constant(get_class_static(), "Type", "Boss",
                                 static_cast<int64_t>(Boss));
}

String MapNode::_to_string() const {
  const char type_char = type_to_char(type);
  const String selected_marker = selected ? "*" : "";

  // Format: "3 (E)*" or coumn 3, type, selected
  return vformat("%d (%c)%s", column, type_char, selected_marker);
}
