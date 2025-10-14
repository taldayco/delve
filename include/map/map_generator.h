#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "map_node.h"
#include <godot_cpp/classes/node.hpp>
#include <optional>

using namespace godot;

class RNGManager;

// Map Generation Config

namespace MapConfig {
constexpr int HEIGHT = 15;
constexpr int WIDTH = 7;
constexpr int PATHS = 6;
constexpr int X_DIST = 30;
constexpr int Y_DIST = 25;
constexpr int PLACEMENT_RANDOMNESS = 30;
} // namespace MapConfig
namespace NodeWeights {
constexpr float ENEMY = 10.0f;
constexpr float WENNY = 2.5f;
constexpr float SHELTER = 4.0f;
constexpr float LOOT = 0.0f;
constexpr float TOTAL = ENEMY + WENNY + SHELTER;
} // namespace NodeWeights

// Map Generator
class MapGenerator : public Node {
  GDCLASS(MapGenerator, Node);

public:
  MapGenerator() = default;
  ~MapGenerator() = default;

  void _enter_tree() override;
  void _ready() override;
  void _exit_tree() override;

  // Public API
  [[nodiscard]] Array generate_map();
  void regenerate_map();

private:
  // Node Weight Container (Tyoe-safe)
  struct NodeTypeWeights {
    float enemy;
    float wenny;
    float shelter;

    // Calculate total weight at compile-time when possible
    [[nodiscard]] constexpr float total() const noexcept {
      return enemy + wenny + shelter;
    }

    // Pick random Node Type Based off weight distribution
    [[nodiscard]] MapNode::Type
    pick_random_type(float random_value) const noexcept;
  };

  // Initial grid struct w/o connection
  [[nodiscard]] Array generate_initial_grid() const;

  // single node at specified position
  [[nodiscard]] Ref<MapNode> create_node(int row, int col) const;

  // Calculate base position for a node (without randomness)
  [[nodiscard]] Vector2 calculate_base_position(int row,
                                                int col) const noexcept;

  // random offset generation
  [[nodiscard]] Vector2 generate_random_offset() const noexcept;
  [[nodiscard]] Array get_starting_points() const;

  NodeTypeWeights weights{.enemy = NodeWeights::ENEMY,
                          .wenny = NodeWeights::WENNY,
                          .shelter = NodeWeights::SHELTER};

  Array map_data;
  RNGManager *rng_manager = nullptr;

protected:
  static void _bind_methods();
};

#endif
