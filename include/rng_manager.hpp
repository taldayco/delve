#pragma once

#include "godot_cpp/classes/ref.hpp"
#include <cstdint>
#include <godot_cpp/classes/random_number_generator.hpp>

using godot::RandomNumberGenerator;
using godot::Ref;

class RNGManager {
public:
  // singleton access
  static RNGManager &get();

  // api
  void randomize();
  void set_seed(uint64_t s);
  float randf(); // wrapper around rng -> randf()
  float randf_range(float a, float b);
  int randi_range(int a, int b);

private:
  RNGManager();
  Ref<RandomNumberGenerator> rng;
};
