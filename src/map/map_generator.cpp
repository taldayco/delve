#include "map/map_generator.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "godot_cpp/variant/vector2i.hpp"
#include "map/map_node.h"
#include "rng_manager.h"

using namespace godot;

void MapGenerator::_bind_methods() {
  ClassDB::bind_method(D_METHOD("generate_map"), &MapGenerator::generate_map);
  ClassDB::bind_method(D_METHOD("_generate_initial_grid"),
                       &MapGenerator::_generate_initial_grid);
}

void MapGenerator::_enter_tree() {
  // Create RNGManager as a child node
  rng_manager = memnew(RNGManager);
  add_child(rng_manager);

  weightDict["enemy"] = ENEMY_NODE_WEIGHT;
  weightDict["wenny"] = WENNY_NODE_WEIGHT;
  weightDict["shelter"] = SHELTER_NODE_WEIGHT;

  random_node_type_total_weight =
      ENEMY_NODE_WEIGHT + WENNY_NODE_WEIGHT + SHELTER_NODE_WEIGHT;

  UtilityFunctions::print("MapGenerator Initialized Weights");
}

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

void MapGenerator::_exit_tree() {
  UtilityFunctions::print("MapGenerator exiting");
}

Array MapGenerator::_generate_initial_grid() {
  Array result;

  for (int i = 0; i < MAP_HEIGHT; i++) {
    Array adjacent_nodes;

    for (int j = 0; j < MAP_WIDTH; j++) {
      Ref<MapNode> current_node;
      current_node.instantiate();
      adjacent_nodes.append(current_node);

      // Use rng_manager instance instead of singleton
      Vector2 offset = Vector2(rng_manager->randf_range(-1.0, 1.0),
                               rng_manager->randf_range(-1.0, 1.0)) *
                       PLACEMENT_RANDOMNESS;

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

Array MapGenerator::generate_map() { return _generate_initial_grid(); }
