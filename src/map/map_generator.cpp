#include "map/map_generator.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "map/map_node.h"

using namespace godot;

void MapGenerator::_bind_methods() {
  ClassDB::bind_method(D_METHOD("generate_map"), &MapGenerator::generate_map);
  ClassDB::bind_method(D_METHOD("_generate_initial_grid"),
                       &MapGenerator::_generate_initial_grid);
}
Array MapGenerator::_generate_initial_grid() {
  Array result;

  for (int i = 0; i < MAP_HEIGHT; i++) {
    Array row;

    for (int j = 0; j < MAP_WIDTH; j++) {

      Ref<MapNode> current_node;
      current_node.instantiate();
      row.append(current_node);

      current_node->set_position(Vector2(j * X_DIST, i * -Y_DIST));
      current_node->set_row(i);
      current_node->set_column(j);
      current_node->set_next_nodes(Array());
    }
    result.append(row);
  }
  return result;
}
