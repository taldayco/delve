// test_mesh_integrity.cpp
// Tests that catch GPU crashes caused by invalid mesh data:
//   - out-of-bounds indices (would cause GPU VRAM fault / VK_ERROR_DEVICE_LOST)
//   - non-finite vertex positions (NaN/Inf corrupt GPU draw calls)
//   - mesh sizes exceeding practical GPU buffer limits
//   - lava mesh integrity
//
// These tests use Config::HEX_SIZE so any change to the constant is exercised.

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

// ── Index bounds ─────────────────────────────────────────────────────────────

// Every basalt index must be < the vertex count of its layer.
// An out-of-bounds index causes GPU VRAM fault (address 0x00000000).
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

// Lava indices must be < lava vertex count.
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

// Triangle lists require index count divisible by 3.
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

// ── Vertex position sanity ────────────────────────────────────────────────────