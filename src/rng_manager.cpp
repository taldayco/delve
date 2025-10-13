#include "rng_manager.h"
#include <godot_cpp/core/class_db.hpp>

RNGManager::RNGManager() {
  rng.instantiate();
  randomize();
}

void RNGManager::_bind_methods() {
  ClassDB::bind_method(D_METHOD("randomize"), &RNGManager::randomize);
  ClassDB::bind_method(D_METHOD("set_seed", "seed"), &RNGManager::set_seed);
  ClassDB::bind_method(D_METHOD("randf"), &RNGManager::randf);
  ClassDB::bind_method(D_METHOD("randf_range", "from", "to"),
                       &RNGManager::randf_range);
  ClassDB::bind_method(D_METHOD("randi_range", "from", "to"),
                       &RNGManager::randi_range);
}

void RNGManager::randomize() noexcept { rng->randomize(); }

void RNGManager::set_seed(uint64_t seed) noexcept { rng->set_seed(seed); }

float RNGManager::randf() noexcept { return rng->randf(); }

float RNGManager::randf_range(float from, float to) noexcept {
  return rng->randf_range(from, to);
}

int RNGManager::randi_range(int from, int to) noexcept {
  return rng->randi_range(from, to);
}
