#include "map/map_generator.h"
#include <algorithm>
#include <iostream>
#include <unordered_set>

void MapGenerator::initialize() {
  // Initialize RNG
  rng.seed(std::random_device{}());

  // Pre-allocate flat storage
  nodes.resize(MapConfig::HEIGHT * MapConfig::WIDTH);

  // Generate map
  generate_initial_grid();
  std::vector<int> starting_columns = get_starting_columns();
  setup_connections(starting_columns);
  setup_boss_node();
  assign_node_types();

  std::cout << "MapGenerator: Ready with " << nodes.size() << " nodes\n";
}

void MapGenerator::create_node(int row, int col) {
  const int i = idx(row, col);
  MapNodeData &node = nodes[i];

  node.row = static_cast<int16_t>(row);
  node.column = static_cast<int16_t>(col);
  node.position = calculate_base_position(row, col) + generate_random_offset();
}

void MapGenerator::generate_initial_grid() {
  for (int row = 0; row < MapConfig::HEIGHT; ++row) {
    for (int col = 0; col < MapConfig::WIDTH; ++col) {
      create_node(row, col);
    }
  }
}

Vector2 MapGenerator::calculate_base_position(int row, int col) const noexcept {
  const float x = col * MapConfig::X_DIST;
  const float y =
      (row == MapConfig::HEIGHT - 1 ? row + 1 : row) * -MapConfig::Y_DIST;
  return Vector2(x, y);
}

Vector2 MapGenerator::generate_random_offset() noexcept {
  const float offset_x = randf_range(-1.0f, 1.0f);
  const float offset_y = randf_range(-1.0f, 1.0f);
  return Vector2(offset_x, offset_y) * MapConfig::PLACEMENT_RANDOMNESS;
}

std::vector<int> MapGenerator::get_starting_columns() {
  std::vector<int> columns;
  columns.reserve(MapConfig::PATHS);

  // Keep generating until we have at least 2 unique columns
  while (true) {
    columns.clear();
    for (int i = 0; i < MapConfig::PATHS; ++i) {
      columns.push_back(randi_range(0, MapConfig::WIDTH - 1));
    }

    std::unordered_set<int> unique_check(columns.begin(), columns.end());
    if (unique_check.size() >= 2) {
      break;
    }
  }

  return columns;
}

void MapGenerator::setup_connections(const std::vector<int> &starting_columns) {
  for (int path = 0; path < starting_columns.size(); ++path) {
    int current_column = starting_columns[path];

    for (int row = 0; row < MapConfig::HEIGHT - 1; ++row) {
      current_column = single_random_connection(row, current_column);
    }
  }
}

int MapGenerator::single_random_connection(int row, int current_column) {
  const int current_idx = idx(row, current_column);
  MapNodeData &current = nodes[current_idx];

  int next_column = -1;

  // Find valid non-crossing connection
  while (next_column < 0 ||
         would_cross_existing_path(row, current_column, next_column)) {
    next_column =
        std::clamp(randi_range(current_column - 1, current_column + 1), 0,
                   MapConfig::WIDTH - 1);
  }

  // Store connection as index
  const int next_idx = idx(row + 1, next_column);
  current.next_indices[current.next_count++] = static_cast<int16_t>(next_idx);

  // Track parent type in child (only if not already set)
  MapNodeData &next = nodes[next_idx];
  if (next.parent_type == MapNodeData::Type::NotAssigned) {
    next.parent_type = current.type;
  }

  return next_column;
}

bool MapGenerator::would_cross_existing_path(int row, int current_column,
                                             int next_column) const {
  // Check right neighbor crossing left
  if (next_column > current_column && current_column < MapConfig::WIDTH - 1) {
    const int right_idx = idx(row, current_column + 1);
    const MapNodeData &right = nodes[right_idx];

    for (int i = 0; i < right.next_count; ++i) {
      const int16_t connected_idx = right.next_indices[i];
      const int connected_col = nodes[connected_idx].column;
      if (connected_col < next_column) {
        return true;
      }
    }
  }

  // Check left neighbor crossing right
  if (next_column < current_column && current_column > 0) {
    const int left_idx = idx(row, current_column - 1);
    const MapNodeData &left = nodes[left_idx];

    for (int i = 0; i < left.next_count; ++i) {
      const int16_t connected_idx = left.next_indices[i];
      const int connected_col = nodes[connected_idx].column;
      if (connected_col > next_column) {
        return true;
      }
    }
  }

  return false;
}

void MapGenerator::setup_boss_node() {
  const int middle_col = MapConfig::WIDTH / 2;
  const int boss_idx = idx(MapConfig::HEIGHT - 1, middle_col);
  nodes[boss_idx].type = MapNodeData::Type::Boss;

  // Connect all active nodes in penultimate row to boss
  const int penultimate_row = MapConfig::HEIGHT - 2;
  for (int col = 0; col < MapConfig::WIDTH; ++col) {
    const int node_idx = idx(penultimate_row, col);
    MapNodeData &node = nodes[node_idx];

    if (node.next_count > 0) {
      node.next_count = 0; // Clear existing
      node.next_indices[node.next_count++] = static_cast<int16_t>(boss_idx);
    }
  }
}

void MapGenerator::assign_node_types() {
  // Single pass - no parent searching needed
  for (int row = 0; row < MapConfig::HEIGHT - 1; ++row) {
    for (int col = 0; col < MapConfig::WIDTH; ++col) {
      const int node_idx = idx(row, col);
      MapNodeData &node = nodes[node_idx];

      if (node.next_count == 0)
        continue; // Skip unconnected

      // Special floor rules
      if (row == 0) {
        node.type = MapNodeData::Type::Enemy;
      } else if (row == 8 || row == MapConfig::HEIGHT - 2) {
        node.type = MapNodeData::Type::Shelter;
      } else {
        // Parent type already tracked during connection phase
        const MapNodeData::Type parent_type = node.parent_type;

        // Build weighted random based on constraints
        float valid_weight = NodeWeights::ENEMY;
        if (row >= 3 && parent_type != MapNodeData::Type::Shelter) {
          valid_weight += NodeWeights::SHELTER;
        }
        if (parent_type != MapNodeData::Type::Wenny) {
          valid_weight += NodeWeights::WENNY;
        }

        const float rand_val = randf_range(0.0f, valid_weight);
        float accumulated = 0.0f;

        accumulated += NodeWeights::ENEMY;
        if (rand_val <= accumulated) {
          node.type = MapNodeData::Type::Enemy;
          continue;
        }

        if (parent_type != MapNodeData::Type::Wenny) {
          accumulated += NodeWeights::WENNY;
          if (rand_val <= accumulated) {
            node.type = MapNodeData::Type::Wenny;
            continue;
          }
        }

        if (row >= 3 && parent_type != MapNodeData::Type::Shelter) {
          node.type = MapNodeData::Type::Shelter;
        } else {
          node.type = MapNodeData::Type::Enemy;
        }
      }
    }
  }
}

void MapGenerator::regenerate_map() {
  rng.seed(std::random_device{}());

  std::fill(nodes.begin(), nodes.end(), MapNodeData{});

  generate_initial_grid();
  std::vector<int> starting_columns = get_starting_columns();
  setup_connections(starting_columns);
  setup_boss_node();
  assign_node_types();

  print_debug_info();
}

MapGenerator::MapData MapGenerator::get_map_data() const {
  return {MapConfig::WIDTH, MapConfig::HEIGHT, &nodes};
}
void MapGenerator::print_debug_info() const {
  int connected_count = 0;
  for (const auto &node : nodes) {
    if (node.next_count > 0)
      ++connected_count;
  }

  std::cout << "Total nodes: " << nodes.size() << "\n";
  std::cout << "Connected nodes: " << connected_count << "\n";
  std::cout << "Boss node at: (" << MapConfig::HEIGHT - 1 << ", "
            << MapConfig::WIDTH / 2 << ")\n";
}
