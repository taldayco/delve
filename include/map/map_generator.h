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

private:
  int MAP_HEIGT = 15; // number of "Floors"
  int MAP_WIDTH = 7;
  int PATHS = 6;
  float ENEMY_NODE_WEIGHT = 10.0f;
  float WENNY_NODE_WEIGHT = 2.5f;
  float SHELTER_NODE_WEIGHT = 4.0f;

  Dictionary weightDict;

  float random_node_type_total_weight = 0.0f;

  PackedInt32Array map_data;

protected:
  static void _bind_methods();

public:
  MapGenerator();
  ~MapGenerator();
};

#endif
