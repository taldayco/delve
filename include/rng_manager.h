#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/ref.hpp>

using namespace godot;

class RNGManager : public Node {
  GDCLASS(RNGManager, Node);

public:
  RNGManager();
  ~RNGManager();

  void randomize();
  void set_seed(uint64_t s);
  float randf();
  float randf_range(float a, float b);
  int randi_range(int a, int b);

protected:
  static void _bind_methods();

private:
  Ref<RandomNumberGenerator> rng;
};
