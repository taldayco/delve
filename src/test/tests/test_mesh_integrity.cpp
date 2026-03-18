#include "test_harness.h"
#include "geometry_metrics.h"
#include "terrain_metrics.h"
#include "config.h"
#include "terrain/map_data.h"
#include "terrain/noise_layers.h"
#include "terrain/noise_composer.h"
#include "terrain/basalt.h"
#include "terrain/lava.h"
#include "terrain/contour.h"
#include "terrain/terrain_mesh.h"
#include "game_state.h"
#include <cmath>
#include <cstdint>

static constexpr int MW = 256;
static constexpr int MH = 256;

static MapData make_map(int seed = 1337) {
    MapData md;
    md.allocate(MW, MH);
    ElevationParams elev; elev.seed = seed;
    RiverParams river;    river.seed = seed + 1;
    WorleyParams worley;  worley.seed = seed + 2;
    CompositionParams comp;
    compose_layers(md, elev, river, worley, comp, nullptr);
    md.columns = generate_basalt_columns_v2(md, Config::HEX_SIZE);
    auto fill = generate_lava_and_void(md, comp.void_chance, worley.seed);
    md.lava_bodies = std::move(fill.lava_bodies);
    md.void_bodies = std::move(fill.void_bodies);
    float interval = 1.0f / comp.terrace_levels;
    extract_contours(md.basalt_height, MW, MH, interval, md.contour_lines, md.band_map);
    simplify_contours(md.contour_lines, 0.5f);
    return md;
}

static TerrainMesh make_mesh(const MapData &md) {
    TerrainState ts; ts.current_palette = 0; ts.map_scale = 1.0f;
    ContourData cd;
    cd.heightmap.assign(md.basalt_height.begin(), md.basalt_height.end());
    cd.contour_lines = md.contour_lines;
    cd.band_map = md.band_map;
    return build_terrain_mesh(ts, md, cd);
}

DELVE_TEST(mesh_basalt_indices_in_bounds) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (size_t li = 0; li < mesh.basalt_layers.size(); ++li) {
        const auto &layer = mesh.basalt_layers[li];
        uint32_t nverts = (uint32_t)layer.vertices.size();
        for (size_t ii = 0; ii < layer.indices.size(); ++ii) {
            if (layer.indices[ii] >= nverts) {
                fprintf(stderr, "  FAIL: layer %zu index[%zu]=%u >= nverts=%u\n",
                        li, ii, layer.indices[ii], nverts);
                return false;
            }
        }
    }
    return true;
}

DELVE_TEST(mesh_lava_indices_in_bounds) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    uint32_t nverts = (uint32_t)mesh.lava_vertices.size();
    for (size_t ii = 0; ii < mesh.lava_indices.size(); ++ii) {
        if (mesh.lava_indices[ii] >= nverts) {
            fprintf(stderr, "  FAIL: lava index[%zu]=%u >= nverts=%u\n",
                    ii, mesh.lava_indices[ii], nverts);
            return false;
        }
    }
    return true;
}

DELVE_TEST(mesh_index_count_multiple_of_three) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (size_t li = 0; li < mesh.basalt_layers.size(); ++li) {
        size_t cnt = mesh.basalt_layers[li].indices.size();
        EXPECT_EQ(cnt % 3, (size_t)0);
    }
    EXPECT_EQ(mesh.lava_indices.size() % 3, (size_t)0);
    return true;
}

DELVE_TEST(mesh_basalt_vertex_positions_finite) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (auto &layer : mesh.basalt_layers) {
        for (auto &v : layer.vertices) {
            EXPECT_FALSE(std::isnan(v.pos_x) || std::isinf(v.pos_x));
            EXPECT_FALSE(std::isnan(v.pos_y) || std::isinf(v.pos_y));
            EXPECT_FALSE(std::isnan(v.pos_z) || std::isinf(v.pos_z));
        }
    }
    return true;
}

DELVE_TEST(mesh_lava_vertex_positions_finite) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (auto &v : mesh.lava_vertices) {
        EXPECT_FALSE(std::isnan(v.pos_x) || std::isinf(v.pos_x));
        EXPECT_FALSE(std::isnan(v.pos_y) || std::isinf(v.pos_y));
        EXPECT_FALSE(std::isnan(v.pos_z) || std::isinf(v.pos_z));
    }
    return true;
}

DELVE_TEST(mesh_contour_vertex_positions_finite) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (auto &v : mesh.contour_vertices) {
        EXPECT_FALSE(std::isnan(v.pos_x) || std::isinf(v.pos_x));
        EXPECT_FALSE(std::isnan(v.pos_y) || std::isinf(v.pos_y));
        EXPECT_FALSE(std::isnan(v.pos_z) || std::isinf(v.pos_z));
    }
    return true;
}

DELVE_TEST(mesh_basalt_vertex_positions_within_map_bounds) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    float max_wx = (float)MW / Config::HEX_SIZE * 1.1f;
    float max_wy = (float)MH / Config::HEX_SIZE * 1.1f;
    float min_wx = -(float)MW / Config::HEX_SIZE * 0.1f;
    float min_wy = -(float)MH / Config::HEX_SIZE * 0.1f;
    for (auto &layer : mesh.basalt_layers) {
        for (auto &v : layer.vertices) {
            if (v.pos_x < min_wx || v.pos_x > max_wx ||
                v.pos_y < min_wy || v.pos_y > max_wy) {
                fprintf(stderr, "  FAIL: vertex pos (%.3f, %.3f) outside map bounds"
                        " [%.1f..%.1f, %.1f..%.1f]\n",
                        v.pos_x, v.pos_y, min_wx, max_wx, min_wy, max_wy);
                return false;
            }
        }
    }
    return true;
}

DELVE_TEST(mesh_basalt_vertex_count_gpu_safe) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    constexpr size_t GPU_VERTEX_LIMIT = 4'000'000;
    size_t total = mesh_vertex_count(mesh);
    if (total > GPU_VERTEX_LIMIT) {
        fprintf(stderr, "  FAIL: basalt vertex count %zu exceeds GPU-safe limit %zu\n",
                total, GPU_VERTEX_LIMIT);
        return false;
    }
    return true;
}

DELVE_TEST(mesh_basalt_index_count_gpu_safe) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    constexpr size_t GPU_INDEX_LIMIT = 12'000'000;
    size_t total = mesh_index_count(mesh);
    if (total > GPU_INDEX_LIMIT) {
        fprintf(stderr, "  FAIL: basalt index count %zu exceeds GPU-safe limit %zu\n",
                total, GPU_INDEX_LIMIT);
        return false;
    }
    return true;
}

DELVE_TEST(mesh_non_empty_when_columns_exist) {
    auto md   = make_map();
    if (md.columns.empty()) return true;
    auto mesh = make_mesh(md);
    EXPECT_GT(mesh_vertex_count(mesh), (size_t)0);
    EXPECT_GT(mesh_index_count(mesh),  (size_t)0);
    return true;
}

DELVE_TEST(mesh_index_to_vertex_ratio_sane) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (auto &layer : mesh.basalt_layers) {
        if (layer.vertices.empty()) continue;
        size_t max_reasonable_indices = layer.vertices.size() * 10;
        if (layer.indices.size() > max_reasonable_indices) {
            fprintf(stderr, "  FAIL: %zu indices for %zu vertices (ratio %.1f > 10)\n",
                    layer.indices.size(), layer.vertices.size(),
                    (double)layer.indices.size() / layer.vertices.size());
            return false;
        }
    }
    return true;
}
