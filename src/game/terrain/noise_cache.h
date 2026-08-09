#pragma once
#include <cstdint>
#include <cstring>
#include <vector>


struct NoiseCache {
  enum Slot { ELEVATION = 0, RIVER = 1, WORLEY = 2, SLOT_COUNT = 3 };

  struct CacheEntry {
    uint64_t param_hash = 0;
    std::vector<float> data;
    std::vector<float> data2;
    std::vector<float> data3;
    bool valid = false;
  };

  CacheEntry entries[SLOT_COUNT];


  template <typename T> static uint64_t hash_params(const T &params) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&params);
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < sizeof(T); ++i) {
      hash ^= bytes[i];
      hash *= 1099511628211ULL;
    }
    return hash;
  }

  bool get(Slot slot, uint64_t param_hash, std::vector<float> &out) const {
    const auto &e = entries[slot];
    if (e.valid && e.param_hash == param_hash) {
      out = e.data;
      return true;
    }
    return false;
  }

  bool get3(Slot slot, uint64_t param_hash, std::vector<float> &out1,
            std::vector<float> &out2, std::vector<float> &out3) const {
    const auto &e = entries[slot];
    if (e.valid && e.param_hash == param_hash) {
      out1 = e.data;
      out2 = e.data2;
      out3 = e.data3;
      return true;
    }
    return false;
  }

  void put(Slot slot, uint64_t param_hash, std::vector<float> data) {
    auto &e = entries[slot];
    e.param_hash = param_hash;
    e.data = std::move(data);
    e.valid = true;
  }

  void put3(Slot slot, uint64_t param_hash, std::vector<float> data1,
            std::vector<float> data2, std::vector<float> data3) {
    auto &e = entries[slot];
    e.param_hash = param_hash;
    e.data = std::move(data1);
    e.data2 = std::move(data2);
    e.data3 = std::move(data3);
    e.valid = true;
  }

  void invalidate_all() {
    for (auto &e : entries)
      e.valid = false;
  }
};
