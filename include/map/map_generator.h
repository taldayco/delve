#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <vector>

using namespace godot;

// Map Generation Config
namespace MapConfig {
constexpr int HEIGHT = 15;
constexpr int WIDTH = 7;
constexpr int PATHS = 6;
constexpr int X_DIST = 30;
constexpr int Y_DIST = 25;
constexpr int PLACEMENT_RANDOMNESS = 30;
constexpr int MAX_CONNECTIONS = 3; // Add this
} // namespace MapConfig

struct MapNodeData {
  enum class Type : uint8_t {
    NotAssigned = 0,
    Enemy = 1,
    Loot = 2,
    Shelter = 3,
    Wenny = 4,
    Boss = 5
  };

  // Data members
  Type type{};
  int16_t row{};
  int16_t column{};
  Vector2 position{};
  uint8_t next_count{};
  int16_t next_indices[MapConfig::MAX_CONNECTIONS]{}; // Indices into flat array
  Type parent_type{};
  bool selected{};

  // Debug helper
  [[nodiscard]] static constexpr char type_to_char(Type t) noexcept {
    const char *str = type_to_string(t);
    return str[0];
  }

  [[nodiscard]] static constexpr const char *type_to_string(Type t) noexcept {
    switch (t) {
    case Type::NotAssigned:
      return "NOT_ASSIGNED";
    case Type::Enemy:
      return "ENEMY";
    case Type::Loot:
      return "LOOT";
    case Type::Shelter:
      return "SHELTER";
    case Type::Wenny:
      return "WENNY";
    case Type::Boss:
      return "BOSS";
    }
    return "UNKNOWN";
  }
};

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
  void regenerate_map();
  [[nodiscard]] Dictionary get_map_data() const; // New export method

private:
  // Flat storage - index = row * WIDTH + col
  std::vector<MapNodeData> nodes;

  Ref<RandomNumberGenerator> rng;

  // Inline accessor
  [[nodiscard]] constexpr int idx(int row, int col) const noexcept {
    return row * MapConfig::WIDTH + col;
  }

  // Generation pipeline
  void generate_initial_grid();
  void create_node(int row, int col);

  [[nodiscard]] Vector2 calculate_base_position(int row,
                                                int col) const noexcept;
  [[nodiscard]] Vector2 generate_random_offset() const noexcept;

  [[nodiscard]] std::vector<int> get_starting_columns() const;
  void setup_connections(const std::vector<int> &starting_columns);

  int single_random_connection(int row, int current_column);
  [[nodiscard]] bool would_cross_existing_path(int row, int current_column,
                                               int next_column) const;

  void setup_boss_node();
  void assign_node_types();
  void print_debug_info() const;

protected:
  static void _bind_methods();
};

#endif
