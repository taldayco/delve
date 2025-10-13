#include "rng_manager.h"
#include <godot_cpp/core/class_db.hpp>

RNGManager::RNGManager() {
  rng.instantiate();
  randomize();
}

RNGManager::~RNGManager() {}

void RNGManager::_bind_methods() {
  ClassDB::bind_method(D_METHOD("randomize"), &RNGManager::randomize);
  ClassDB::bind_method(D_METHOD("set_seed", "seed"), &RNGManager::set_seed);
  ClassDB::bind_method(D_METHOD("randf"), &RNGManager::randf);
  ClassDB::bind_method(D_METHOD("randf_range", "from", "to"),
                       &RNGManager::randf_range);
  ClassDB::bind_method(D_METHOD("randi_range", "from", "to"),
                       &RNGManager::randi_range);
}

void RNGManager::randomize() { rng->randomize(); }
void RNGManager::set_seed(uint64_t s) { rng->set_seed(s); }
float RNGManager::randf() { return rng->randf(); }
float RNGManager::randf_range(float a, float b) {
  return rng->randf_range(a, b);
}
int RNGManager::randi_range(int a, int b) { return rng->randi_range(a, b); }
