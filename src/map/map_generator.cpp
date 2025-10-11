#include "map/map_generator.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "godot_cpp/variant/vector2i.hpp"
#include "map/map_node.h"
#include "rng_manager.hpp"

using namespace godot;

void MapGenerator::_bind_methods() {
  ClassDB::bind_method(D_METHOD("generate_map"), &MapGenerator::generate_map);
  ClassDB::bind_method(D_METHOD("_generate_initial_grid"),
                       &MapGenerator::_generate_initial_grid);
}
Array MapGenerator::_generate_initial_grid() {
  Array result;

  for (int i = 0; i < MAP_HEIGHT; i++) {
    Array adjacent_nodes;

    for (int j = 0; j < MAP_WIDTH; j++) {

      Ref<MapNode> current_node;
      current_node.instantiate();
      adjacent_nodes.append(current_node);

      // add random placement offset
      Vector2 offset = Vector2(RNGManager::get().randf_range(-1.0, 1.0),
                               RNGManager::get().randf_range(-1.0, 1.0)) *
                       PLACEMENT_RANDOMNESS;

      // Add space between boss node and normal nodes
      if (i == MAP_HEIGHT - 1) {
        current_node->set_position(Vector2(j * X_DIST, (i + 1) * -Y_DIST) +
                                   offset);
      } else {

        current_node->set_position(Vector2(j * X_DIST, i * -Y_DIST) + offset);
      }
      current_node->set_row(i);
      current_node->set_column(j);
      current_node->set_next_nodes(Array());
    }
    result.append(adjacent_nodes);
  }
  return result;
}

// Array MapGenerator::generate_map() {};

void MapGenerator::_ready() {
  map_data = _generate_initial_grid();
  UtilityFunctions::print("Grid Rows: ", map_data.size());

  if (map_data.size() > 0) {
    Array first_row = map_data[0];
    UtilityFunctions::print("First Row columns: ", first_row.size());

    Ref<MapNode> first_node = first_row[0];
    UtilityFunctions::print("First Node: ", first_node->_to_string());
    UtilityFunctions::print("Position: ", first_node->get_position());
  }
}
