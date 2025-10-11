#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_int32_array.hpp"
#include <godot_cpp/classes/node.hpp>

using namespace godot;

class MapGenerator : public Node {
  GDCLASS(MapGenerator, Node);

public:
  MapGenerator() = default;
  ~MapGenerator() = default;

  // override placehoder godot  vitual methods
  // that are called when a node is added to the scene tree.
  void _enter_tree() override;
  void _ready() override;
  void _exit_tree() override;

  // methods required to generate map
  Array _generate_initial_grid();
  Array generate_map();

private:
  int MAP_HEIGHT = 15; // number of "Floors"
  int MAP_WIDTH = 7;
  int PATHS = 6;
  int X_DIST = 30;
  int Y_DIST = 25;
  int PLACEMENT_RANDOMNESS = 30;
  float ENEMY_NODE_WEIGHT = 10.0f;
  float WENNY_NODE_WEIGHT = 2.5f;
  float SHELTER_NODE_WEIGHT = 4.0f;

  Dictionary weightDict;

  float random_node_type_total_weight = 0.0f;

  Array map_data;

protected:
  static void _bind_methods();
};

#endif
