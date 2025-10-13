#include "map/map_generator.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "map/map_node.h"
#include "rng_manager.h"

using namespace godot;

void MapGenerator::_bind_methods() {
  ClassDB::bind_method(D_METHOD("generate_map"), &MapGenerator::generate_map);
}

void MapGenerator::_enter_tree() {
  // Create RNGManager as a child node
  rng_manager = memnew(RNGManager);
  rng_manager->set_name("RNGManager");
  add_child(rng_manager);

  UtilityFunctions::print(
      "MapGenerator: Initialized with weights - ", "Enemy: ", weights.enemy,
      ", ", "Wenny: ", weights.wenny, ", ", "Shelter: ", weights.shelter, ", ",
      "Total: ", weights.total());
}

void MapGenerator::_ready() {
  // Generate initial map
  map_data = generate_initial_grid();

  UtilityFunctions::print("MapGenerator: Generated grid with ", map_data.size(),
                          " rows");

  // Debug out for first node
  if (map_data.size() > 0) {
    Array first_row = map_data[0];
    UtilityFunctions::print(" First Row has ", first_row.size(), " columns");

    if (first_row.size() > 0) {
      const Ref<MapNode> first_node = first_row[0];
      if (first_node.is_valid()) {
        UtilityFunctions::print(" First node: ", first_node->_to_string());
        UtilityFunctions::print(" Position", first_node->get_position());
      }
    }
  }
}

void MapGenerator::_exit_tree() {
  UtilityFunctions::print("MapGenerator: Exiting tree");
  rng_manager = nullptr;
}

MapNode::Type MapGenerator::NodeTypeWeights::pick_random_type(
    float random_value) const noexcept {
  using enum MapNode::Type;

  float accumulated = 0.0f;

  accumulated += enemy;
  if (random_value <= accumulated)
    return Enemy;

  accumulated += wenny;
  if (random_value <= accumulated)
    return Wenny;

  accumulated += shelter;
  if (random_value <= accumulated)
    return Shelter;

  return Enemy;
}

// Map Generation
Array MapGenerator::generate_map() { return generate_initial_grid(); }

Array MapGenerator::generate_initial_grid() const {

  Array result;
  result.resize(MapConfig::HEIGHT);

  for (int row = 0; row < MapConfig::HEIGHT; ++row) {
    TypedArray<MapNode> row_nodes;
    row_nodes.resize(MapConfig::WIDTH);

    for (int col = 0; col < MapConfig::WIDTH; ++col) {
      row_nodes[col] = create_node(row, col);
    }
    result[row] = row_nodes;
  }
  return result;
}

// node creation helpers
Ref<MapNode> MapGenerator::create_node(int row, int col) const {
  Ref<MapNode> node;
  node.instantiate();

  // calc Position
  const Vector2 base_pos = calculate_base_position(row, col);
  const Vector2 offset = generate_random_offset();
  node->set_position(base_pos + offset);

  // set grid coords
  node->set_row(row);
  node->set_column(col);

  return node;
}

Vector2 MapGenerator::calculate_base_position(int row, int col) const noexcept {
  const float x = col * MapConfig::X_DIST;

  // last row is offset by one additional Y_DIST to give Boss space
  const float y =
      (row == MapConfig::HEIGHT - 1 ? row + 1 : row) * -MapConfig::Y_DIST;

  return Vector2(x, y);
}

Vector2 MapGenerator::generate_random_offset() const noexcept {
  const float offset_x = rng_manager->randf_range(-1.0f, 1.0f);
  const float offset_y = rng_manager->randf_range(-1.0f, 1.0f);

  return Vector2(offset_x, offset_y) * MapConfig::PLACEMENT_RANDOMNESS;
}
