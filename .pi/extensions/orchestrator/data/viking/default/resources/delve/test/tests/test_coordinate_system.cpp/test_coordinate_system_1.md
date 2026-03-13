// test_coordinate_system.cpp
// Tests for the hex coordinate system and Config constants.
//
// These tests specifically guard against regressions where Config::HEX_SIZE
// or related constants are changed in ways that break the rendering pipeline:
//   - GPU cluster buffer overflow (MAP_WIDTH_UNITS / tile_px too large)
//   - Hex column count explosion (too-small HEX_SIZE → millions of columns)
//   - Broken pixel ↔ hex roundtrip at the actual game constant
//   - Infinite/NaN positions from coordinate conversion at map scale

#include "test_harness.h"
#include "config.h"
#include "terrain/hex.h"
#include "terrain/map_data.h"
#include "terrain/noise_layers.h"
#include "terrain/noise_composer.h"
#include "terrain/basalt.h"
#include "terrain/contour.h"
#include <cmath>
#include <vector>

// ── Config constant sanity ────────────────────────────────────────────────────

// HEX_SIZE must be large enough that the hex grid over a 1024×1024 map does
// not produce an unmanageable number of columns.
// With HEX_SIZE=8: ~6 k columns on a 256×256 test map.
// With HEX_SIZE=2: ~96 k columns on the same map — 16× more, GPU limits exceeded.
// Threshold: HEX_SIZE >= 4 ensures < 25k columns per 256×256 tile.
DELVE_TEST(config_hex_size_large_enough) {
    EXPECT_GT(Config::HEX_SIZE, 3.9f);
    return true;
}

// MAP_WIDTH_UNITS must stay below the cluster tile count limit.
// The renderer's cluster shaders index into a flat SSBO using:
//   cluster_idx = cluster_x + cluster_y * tiles_x + slice * tiles_x * tiles_y
// If MAP_WIDTH_UNITS is too large the world-space → cluster-index mapping wraps
// or reads null memory, producing VK_ERROR_DEVICE_LOST (GPU fault at 0x00000000).
// With HEX_SIZE=8:  MAP_WIDTH_UNITS = 128  → safe
// With HEX_SIZE=2:  MAP_WIDTH_UNITS = 512  → exceeds safe range → GPU crash
// Threshold 256: catches HEX_SIZE=2 (→512) while passing HEX_SIZE≥4 (→256).
DELVE_TEST(config_map_width_units_cluster_safe) {
    float map_width_units = (float)Config::MAP_COLS / Config::HEX_SIZE;
    if (map_width_units > 256.0f) {
        fprintf(stderr, "  FAIL: MAP_WIDTH_UNITS=%.1f > 256; "
                "cluster shader will read OOB (HEX_SIZE=%.1f is too small)\n",
                map_width_units, Config::HEX_SIZE);
        return false;
    }
    return true;
}

// LAVA_GRID_SPACING must be large enough to avoid degenerate lava meshes.
// With LAVA_GRID_SPACING=2.5 and lava bodies up to 50k pixels wide, the grid
// could produce 20k×20k=400M cells — far beyond the MAX_LAVA_GRID_CELLS guard.
// Minimum safe value: grid spacing >= HEX_SIZE / 4.
DELVE_TEST(config_lava_grid_spacing_sane) {
    EXPECT_GT(Config::LAVA_GRID_SPACING, Config::HEX_SIZE / 4.0f - 0.01f);
    return true;
}

// ── Hex ↔ pixel roundtrip with Config::HEX_SIZE ──────────────────────────────

// Roundtrip must be accurate for the actual game constant, not just 8.0.
DELVE_TEST(hex_roundtrip_with_config_hex_size) {
    int total = 0, accurate = 0;
    for (int q = -15; q <= 15; ++q) {
        for (int r = -15; r <= 15; ++r) {
            float px, py;
            hex_to_pixel(q, r, Config::HEX_SIZE, px, py);
            HexCoord back = pixel_to_hex(px, py, Config::HEX_SIZE);
            ++total;
            if (back.q == q && back.r == r) ++accurate;
        }
    }
    float accuracy = (float)accurate / total;
    if (accuracy < 1.0f) {
        fprintf(stderr, "  FAIL: hex roundtrip accuracy %.4f < 1.0 at HEX_SIZE=%.1f\n",
                accuracy, Config::HEX_SIZE);
        return false;
    }
    return true;
}