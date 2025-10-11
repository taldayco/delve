#ifndef MAP_NODE_H
#define MAP_NODE_H

#include "godot_cpp/variant/string.hpp"
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

class MapNode : public Resource {
  GDCLASS(MapNode, Resource);

public:
  enum Type { NOT_ASSIGNED, ENEMY, LOOT, SHELTER, WENNY, BOSS };

  String _to_string() const;

private:
  Type type = NOT_ASSIGNED;
  int row = 0;
  int column = 0;
  Vector2 position;
  Array next_nodes;
  bool selected = false;

protected:
  static void _bind_methods();

public:
  // getters and setters
  void set_type(Type p_type);
  Type get_type() const;

  void set_row(int p_row) { row = p_row; }
  int get_row() const { return row; }

  void set_column(int p_column) { column = p_column; }
  int get_column() const { return column; }

  void set_position(Vector2 p_position) { position = p_position; }
  Vector2 get_position() const { return position; }

  void set_next_nodes(const Array &p_next_nodes) { next_nodes = p_next_nodes; }
  Array get_next_nodes() const { return next_nodes; }

  void set_selected(bool p_selected) { selected = p_selected; }
  bool is_selected() const { return selected; }
};

VARIANT_ENUM_CAST(MapNode::Type);

#endif // MAP_NODE_H
