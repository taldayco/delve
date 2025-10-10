#include "map/map_node.h"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/variant.hpp"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void MapNode::_bind_methods() {

  ClassDB::bind_method(D_METHOD("set_type", "type"), &MapNode::set_type);
  ClassDB::bind_method(D_METHOD("get_type"), &MapNode::get_type);

  ClassDB::bind_method(D_METHOD("set_row", "row"), &MapNode::set_row);
  ClassDB::bind_method(D_METHOD("get_row"), &MapNode::get_row);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "row"), "set_row", "get_row");

  ClassDB::bind_method(D_METHOD("set_column", "column"), &MapNode::set_column);
  ClassDB::bind_method(D_METHOD("get_column"), &MapNode::get_column);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "column"), "set_column",
               "get_column");

  ClassDB::bind_method(D_METHOD("set_position", "position"),
                       &MapNode::set_position);
  ClassDB::bind_method(D_METHOD("get_position"), &MapNode::get_position);
  ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "position"), "set_position",
               "get_position");

  ClassDB::bind_method(D_METHOD("set_next_nodes", "next_nodes"),
                       &MapNode::set_next_nodes);
  ClassDB::bind_method(D_METHOD("get_next_nodes"), &MapNode::get_next_nodes);
  ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "next_nodes",
                            PROPERTY_HINT_ARRAY_TYPE, "node"),
               "set_next_nodes", "get_next_nodes");

  ClassDB::bind_method(D_METHOD("set_selected", "selected"),
                       &MapNode::set_selected);
  ClassDB::bind_method(D_METHOD("is_selected"), &MapNode::is_selected);
  ADD_PROPERTY(PropertyInfo(Variant::BOOL, "selected"), "set_selected",
               "is_selected");

  BIND_ENUM_CONSTANT(NOT_ASSIGNED);
  BIND_ENUM_CONSTANT(ENEMY);
  BIND_ENUM_CONSTANT(LOOT);
  BIND_ENUM_CONSTANT(SHELTER);
  BIND_ENUM_CONSTANT(WENNY);
  BIND_ENUM_CONSTANT(BOSS);
}

void MapNode::set_type(Type p_type) { type = p_type; }
MapNode::Type MapNode::get_type() const { return type; }

void MapNode::set_row(int p_row) { row = p_row; }
int MapNode::get_row() const { return row; }

void MapNode::set_column(int p_column) { column = p_column; }
int MapNode::get_column() const { return column; }

void MapNode::set_position(Vector2 p_position) { position = p_position; }
Vector2 MapNode::get_position() const { return position; }

void MapNode::set_next_nodes(const Array &p_next_nodes) {
  next_nodes = p_next_nodes;
}
Array MapNode::get_next_nodes() const { return next_nodes; }

void MapNode::set_selected(bool p_selected) { selected = p_selected; }
bool MapNode::is_selected() const { return selected; }

// for debugging
String MapNode::_to_string() const {

  // create an array of strings
  static const char *TYPE_NAMES[] = {"NOT_ASSIGNED", "ENEMY", "LOOT",
                                     "SHELTER",      "WENNY", "BOSS"};

  // use the index from TYPE enum to get the corresponding string from our array
  String type_name = TYPE_NAMES[type];        // e.g. "WENNY:
  String first_char = type_name.substr(0, 1); //"W"

  return vformat("%s (%s)", column, first_char);
}
