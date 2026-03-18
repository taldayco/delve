#include "terrain/map_util.h"
#include "config.h"
#include <algorithm>
#include <random>
#include <unordered_map>
#include <vector>
#include <cmath>

float sample_world_height(const MapData &map, float wx, float wy) {
    if (map.basalt_height.empty()) return 0.0f;

    float px = wx * Config::HEX_SIZE;
    float py = wy * Config::HEX_SIZE;

    int x0 = (int)px;
    int y0 = (int)py;
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    x0 = std::max(0, std::min(x0, map.width  - 1));
    y0 = std::max(0, std::min(y0, map.height - 1));
    x1 = std::max(0, std::min(x1, map.width  - 1));
    y1 = std::max(0, std::min(y1, map.height - 1));

    float tx = px - (int)px;
    float ty = py - (int)py;

    float v00 = map.basalt_height[y0 * map.width + x0];
    float v10 = map.basalt_height[y0 * map.width + x1];
    float v01 = map.basalt_height[y1 * map.width + x0];
    float v11 = map.basalt_height[y1 * map.width + x1];

    float top    = v00 + (v10 - v00) * tx;
    float bottom = v01 + (v11 - v01) * tx;
    return top + (bottom - top) * ty;
}

float sphere_trace_height(const MapData &map, float wx, float wy, float radius) {
    float max_h = sample_world_height(map, wx, wy);
    if (radius <= 0.0f) return max_h;

    for (int i = 0; i < 8; ++i) {
        float angle = i * (2.0f * 3.14159265358979323846f / 8.0f);
        float sx = wx + radius * cosf(angle);
        float sy = wy + radius * sinf(angle);
        float h = sample_world_height(map, sx, sy);
        if (h > max_h) max_h = h;
    }
    return max_h;
}

glm::vec3 sample_terrain_normal(const MapData &map, float wx, float wy) {
    constexpr float eps = 0.05f;
    float hL = sample_world_height(map, wx - eps, wy);
    float hR = sample_world_height(map, wx + eps, wy);
    float hD = sample_world_height(map, wx, wy - eps);
    float hU = sample_world_height(map, wx, wy + eps);
    glm::vec3 n(-( hR - hL) / (2.0f * eps),
                -(hU - hD) / (2.0f * eps),
                1.0f);
    return glm::normalize(n);
}

HexColumn find_spawn_column(const MapData &map, uint32_t seed) {
    if (map.columns.empty()) return HexColumn{};

    std::unordered_map<HexCoord, const HexColumn *, HexHash> lookup;
    lookup.reserve(map.columns.size());
    for (const auto &col : map.columns)
        lookup[{col.q, col.r}] = &col;

    static const int dx[6] = { 1,  0, -1, -1,  0,  1};
    static const int dy[6] = { 0,  1,  1,  0, -1, -1};

    std::vector<bool> visited(map.columns.size(), false);

    std::unordered_map<HexCoord, size_t, HexHash> col_index;
    col_index.reserve(map.columns.size());
    for (size_t i = 0; i < map.columns.size(); ++i)
        col_index[{map.columns[i].q, map.columns[i].r}] = i;

    size_t best_start = 0;
    size_t best_size  = 0;
    std::vector<size_t> best_component;

    std::vector<size_t> queue;
    queue.reserve(256);

    for (size_t start = 0; start < map.columns.size(); ++start) {
        if (visited[start]) continue;

        queue.clear();
        queue.push_back(start);
        visited[start] = true;
        std::vector<size_t> component;

        for (size_t qi = 0; qi < queue.size(); ++qi) {
            size_t cur = queue[qi];
            component.push_back(cur);
            const HexColumn &c = map.columns[cur];
            for (int d = 0; d < 6; ++d) {
                HexCoord nb{c.q + dx[d], c.r + dy[d]};
                auto it = col_index.find(nb);
                if (it == col_index.end()) continue;
                size_t ni = it->second;
                if (!visited[ni]) {
                    visited[ni] = true;
                    queue.push_back(ni);
                }
            }
        }

        if (component.size() > best_size) {
            best_size      = component.size();
            best_component = std::move(component);
        }
    }

    if (best_component.empty()) return map.columns[0];

    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> dist(0, best_component.size() - 1);
    return map.columns[best_component[dist(rng)]];
}
