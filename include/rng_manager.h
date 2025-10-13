#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/ref.hpp>

using namespace godot;

class RNGManager : public Node {
  GDCLASS(RNGManager, Node);

public:
  RNGManager();
  ~RNGManager() override = default;

  // core RNG funcs
  void randomize() noexcept;
  void set_seed(uint64_t seed) noexcept;

  [[nodiscard]] float randf() noexcept;
  [[nodiscard]] float randf_range(float from, float to) noexcept;
  [[nodiscard]] int randi_range(int from, int to) noexcept;

  [[nodiscard]] const Ref<RandomNumberGenerator> &get_rng() const noexcept {
    return rng;
  }

protected:
  static void _bind_methods();

private:
  Ref<RandomNumberGenerator> rng;
};
