#include "rng_manager.hpp"
#include <cstdint>

RNGManager::RNGManager() {
  rng.instantiate();
  randomize();
}

RNGManager &RNGManager::get() {
  static RNGManager instance;
  return instance;
}

void RNGManager::randomize() { rng->randomize(); }

void RNGManager::set_seed(uint64_t s) { rng->set_seed(s); }

float RNGManager::randf() { return rng->randf(); }

float RNGManager::randf_range(float a, float b) {
  return rng->randf_range(a, b);
}

int RNGManager::randi_range(int a, int b) { return rng->randi_range(a, b); }
