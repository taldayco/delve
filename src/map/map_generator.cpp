#include "map/map_generator.h"
#include "godot_cpp/classes/ref.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "map/map_node.h"
#include "rng_manager.h"
#include <algorithm>
#include <unordered_set>
#include <vector>

using namespace godot;

void MapGenerator::_bind_methods() {
  ClassDB::bind_method(D_METHOD("generate_map"), &MapGenerator::generate_map);
  ClassDB::bind_method(D_METHOD("regenerate_map"),
                       &MapGenerator::regenerate_map);
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
Array MapGenerator::generate_map() {
  map_data = generate_initial_grid();
  Array starting_nodes = get_starting_nodes();

  setup_connections(starting_nodes);
  setup_boss_node();
  assign_node_types();

  return map_data;
}

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

//  Map regeneration
void MapGenerator::regenerate_map() {
  if (rng_manager) {
    rng_manager->randomize();
  }
  map_data = generate_initial_grid();

  Array starting_nodes = get_starting_nodes();

  UtilityFunctions::print("Starting points: ", starting_nodes);

  UtilityFunctions::print("MapGenerator: Regenerated map", map_data.size(),
                          " rows");

  if (map_data.size() > 0) {
    Array first_row = map_data[0];
    UtilityFunctions::print(" First Row has ", first_row.size(), " nodes:");

    for (int i = 0; i < first_row.size(); ++i) {
      const Ref<MapNode> node = first_row[i];
      if (node.is_valid()) {
        UtilityFunctions::print(" [", i, "]", node->_to_string(), " at  ",
                                node->get_position());
      }
    }
  }
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

Array MapGenerator::get_starting_nodes() const {
  std::vector<int> y_coords;
  y_coords.reserve(MapConfig::PATHS);

  while (true) {
    y_coords.clear();

    for (int i = 0; i < MapConfig::PATHS; ++i) {
      y_coords.push_back(rng_manager->randi_range(0, MapConfig::WIDTH - 1));
    }
    std::unordered_set<int> unique_check(y_coords.begin(), y_coords.end());

    if (unique_check.size() >= 2) {
      break;
    }
  }

  Array result;
  result.resize(MapConfig::PATHS);
  for (int i = 0; i < MapConfig::PATHS; ++i) {
    result[i] = y_coords[i];
  }
  return result;
}

void MapGenerator::setup_connections(Array &starting_nodes) {
  const int num_paths = starting_nodes.size();

  for (int j = 0; j < num_paths; ++j) {
    int current_column = static_cast<int>(starting_nodes[j]);

    for (int i = 0; i < MapConfig::HEIGHT; ++i) {
      current_column = single_random_connection(i, current_column);
    }
  }

  // For debugging, prints connected nodes per floor
  for (int i = 0; i < map_data.size(); ++i) {
    const Array floor = map_data[i];

    String connected_nodes = "Floor " + String::num_int64(i) + ": ";

    for (int j = 0; j < floor.size(); ++j) {
      const Ref<MapNode> node = floor[j];
      if (node.is_valid() && node->get_next_nodes().size() > 0) {
        connected_nodes += String::num_int64(j) + " (N), ";
      }
    }
    UtilityFunctions::print(connected_nodes);
  }
}

int MapGenerator::single_random_connection(int row, int current_column) {

  // get current node
  const Array row_array = map_data[row];
  Ref<MapNode> current_node = row_array[current_column];

  Ref<MapNode> next_node;

  // keep trying until we get a non crossing connection
  while (next_node.is_null() ||
         would_cross_existing_path(row, current_column, next_node)) {

    const int random_column = std::clamp(
        rng_manager->randi_range(current_column - 1, current_column + 1), 0,
        MapConfig::WIDTH - 1);

    // get node from next row
    const Array next_row_array = map_data[row + 1];
    next_node = next_row_array[random_column];
  }
  // establish connection
  current_node->add_next_node(next_node);

  // return the column
  return next_node->get_column();
}

bool MapGenerator::would_cross_existing_path(
    int row, int current_column, const Ref<MapNode> &next_node) const {
  if (next_node.is_null()) {
    return false;
  }
  const Array current_row = map_data[row];
  const int next_column = next_node->get_column();

  // Get adjacent neighbors, defaults to null
  Ref<MapNode>(left_neighbor);
  Ref<MapNode>(right_neighbor);

  if (current_column > 0) {
    left_neighbor = current_row[current_column - 1];
  }

  if (current_column < MapConfig::WIDTH - 1) {
    right_neighbor = current_row[current_column + 1];
  }

  // Don't cross right if right neighbor goes left
  if (!right_neighbor.is_null() && next_column > current_column) {
    const Array right_next_nodes = right_neighbor->get_next_nodes();
    const int count = right_next_nodes.size();

    for (int i = 0; i < count; ++i) {
      const Ref<MapNode> next_node = right_next_nodes[i];
      if (!next_node.is_null() && next_node->get_column() < next_column) {
        return true;
      }
    }
  }

  // Don't cross left if left neigbor goes right

  if (!left_neighbor.is_null() && next_column < current_column) {
    const Array left_next_rooms = left_neighbor->get_next_nodes();
    const int count = left_next_rooms.size();

    for (int i = 0; i < count; ++i) {
      const Ref<MapNode> next_room = left_next_rooms[i];
      if (!next_room.is_null() && next_room->get_column() > next_column) {
        return true;
      }
    }
  }

  return false;
}

void MapGenerator::setup_boss_node() {
  // get middle column
  const int middle = static_cast<int>(MapConfig::WIDTH * 0.5);

  // get boss node from top row, middle column
  const Array boss_row = map_data[MapConfig::HEIGHT - 1];
  Ref<MapNode> boss_node = boss_row[middle];

  // get second-to-last row where paths converge
  const Array penultimate_row = map_data[MapConfig::HEIGHT - 2];

  // connect all active paths to boss
  for (int j = 0; j < MapConfig::WIDTH; ++j) {
    Ref<MapNode> current_node = penultimate_row[j];

    if (current_node.is_valid() && current_node->has_next_nodes()) {
      // clear existing connections and redirect to boss
      current_node->clear_next_nodes();
      current_node->add_next_node(boss_node);
    }
  }
  boss_node->set_type(MapNode::Type::Boss);
}

void MapGenerator::assign_node_types() {
  std::vector<std::vector<MapNode::Type>> parent_types;
  parent_types.resize(MapConfig::HEIGHT);

  for (int i = 0; i < MapConfig::HEIGHT; ++i) {
    parent_types[i].resize(MapConfig::WIDTH);
  }

  for (int i = 0; i < MapConfig::HEIGHT - 1; ++i) {
    const Array floor = map_data[i];

    for (int j = 0; j < floor.size(); ++j) {
      const Ref<MapNode> node = floor[j];
      if (!node.is_valid())
        continue;

      const TypedArray<MapNode> next_nodes = node->get_next_nodes();
      for (int k = 0; k < next_nodes.size(); ++k) {
        const Ref<MapNode> child = next_nodes[k];
        if (child.is_valid()) {
          parent_types[child->get_row()][child->get_column()] =
              node->get_type();
        }
      }
    }
  }

  const float total_weight = weights.total();

  for (int i = 0; i < MapConfig::HEIGHT - 1; ++i) {
    const Array floor = map_data[i];

    for (int j = 0; j < floor.size(); ++i) {
      Ref<MapNode> node = floor[j];
      if (!node.is_valid() || !node->has_next_nodes())
        continue;

      // special floor rules
      if (i == 0) {
        node->set_type(MapNode::Type::Enemy);
      } else if (i == 8) {
        node->set_type(MapNode::Type::Shelter);
      } else if (i == MapConfig::HEIGHT - 2) {
        node->set_type(MapNode::Type::Shelter);
      } else {
        const MapNode::Type parent_type = parent_types[i][j];

        std::vector<MapNode::Type> valid_types;
        valid_types.reserve(3);

        // check enemy
        valid_types.push_back(MapNode::Type::Enemy);

        // check shelter
        if (i >= 3 && parent_type != MapNode::Type::Shelter) {
          valid_types.push_back(MapNode::Type::Shelter);
        }
        // check wenny
        if (parent_type != MapNode::Type::Wenny) {
          valid_types.push_back(MapNode::Type::Wenny);
        }
        if (valid_types.size() == 1) {
          node->set_type(valid_types[0]);
        } else {
          float valid_weight = 0.0f;
          for (const auto type : valid_types) {
            if (type == MapNode::Type::Enemy)
              valid_weight += weights.enemy;
            else if (type == MapNode::Type::Wenny)
              valid_weight += weights.wenny;
            else if (type == MapNode::Type::Shelter)
              valid_weight += weights.shelter;
          }
          const float rand_val = rng_manager->randf_range(0.0f, valid_weight);
          float accumulated = 0.0f;

          for (const auto type : valid_types) {
            if (type == MapNode::Type::Enemy)
              accumulated += weights.enemy;
            else if (type == MapNode::Type::Wenny)
              accumulated += weights.wenny;
            else if (type == MapNode::Type::Shelter)
              accumulated += weights.shelter;

            if (rand_val <= accumulated) {
              node->set_type(type);
              break;
            }
          }
        }
      }
    }
  }
}
Vector2 MapGenerator::generate_random_offset() const noexcept {
  const float offset_x = rng_manager->randf_range(-1.0f, 1.0f);
  const float offset_y = rng_manager->randf_range(-1.0f, 1.0f);

  return Vector2(offset_x, offset_y) * MapConfig::PLACEMENT_RANDOMNESS;
}
