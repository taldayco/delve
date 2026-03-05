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

// hex_to_pixel must produce finite coordinates for any pixel on the map.
DELVE_TEST(hex_to_pixel_finite_for_map_range) {
    // Hex coords spanning the full map at Config::HEX_SIZE.
    int q_max = (int)(Config::MAP_COLS / Config::HEX_SIZE) + 5;
    int r_max = (int)(Config::MAP_ROWS / Config::HEX_SIZE) + 5;
    for (int q = -2; q <= q_max; q += (q_max / 10 + 1)) {
        for (int r = -2; r <= r_max; r += (r_max / 10 + 1)) {
            float px, py;
            hex_to_pixel(q, r, Config::HEX_SIZE, px, py);
            if (!std::isfinite(px) || !std::isfinite(py)) {
                fprintf(stderr, "  FAIL: hex_to_pixel(%d,%d) → (%.3f, %.3f) not finite\n",
                        q, r, px, py);
                return false;
            }
        }
    }
    return true;
}

// pixel_to_hex must return valid (finite) coordinates for every map pixel.
DELVE_TEST(pixel_to_hex_valid_for_all_map_pixels) {
    // Spot-check a grid across the full map.
    int step = std::max(1, Config::MAP_COLS / 32);
    for (int y = 0; y < Config::MAP_ROWS; y += step) {
        for (int x = 0; x < Config::MAP_COLS; x += step) {
            HexCoord h = pixel_to_hex((float)x, (float)y, Config::HEX_SIZE);
            // Coords are integers — just check they're within a sane range.
            int range = Config::MAP_COLS + Config::MAP_ROWS;
            if (std::abs(h.q) > range || std::abs(h.r) > range) {
                fprintf(stderr, "  FAIL: pixel_to_hex(%d,%d) → (%d,%d) out of range\n",
                        x, y, h.q, h.r);
                return false;
            }
        }
    }
    return true;
}

// Neighboring hex cells must be exactly HEX_SIZE * 1.5 apart in X (pointy-top layout).
DELVE_TEST(hex_neighbor_spacing_correct) {
    float px0, py0, px1, py1;
    hex_to_pixel(0, 0, Config::HEX_SIZE, px0, py0);
    hex_to_pixel(1, 0, Config::HEX_SIZE, px1, py1);
    float dx = std::abs(px1 - px0);
    // For flat-top: dx = HEX_SIZE * 1.5
    float expected = Config::HEX_SIZE * 1.5f;
    EXPECT_NEAR(dx, expected, 0.01f);
    return true;
}

// ── Column count regression ───────────────────────────────────────────────────

// With Config::HEX_SIZE, the number of columns generated on a 256×256 test map
// must stay within GPU-safe bounds. If HEX_SIZE shrinks, columns explode and
// the GPU index/vertex buffers overflow.
DELVE_TEST(column_count_sane_with_config_hex_size) {
    static constexpr int W = 256, H = 256;
    MapData md;
    md.allocate(W, H);
    ElevationParams elev; elev.seed = 42;
    RiverParams river;    river.seed = 43;
    WorleyParams worley;  worley.seed = 44;
    CompositionParams comp;
    compose_layers(md, elev, river, worley, comp, nullptr);
    md.columns = generate_basalt_columns_v2(md, Config::HEX_SIZE);

    // On a 256×256 map with HEX_SIZE=8: expect < 4000 columns.
    // With HEX_SIZE=2 (broken): would be ~25 000+ columns on this map size.
    // Threshold = (256*256) / (hex_area) * 2.0 safety factor.
    float hex_area = 2.598f * Config::HEX_SIZE * Config::HEX_SIZE;
    size_t theoretical_max = (size_t)((W * H / hex_area) * 2.0f) + 100;

    if (md.columns.size() > theoretical_max) {
        fprintf(stderr, "  FAIL: %zu columns on %dx%d map with HEX_SIZE=%.1f "
                "(theoretical max ≈ %zu)\n",
                md.columns.size(), W, H, Config::HEX_SIZE, theoretical_max);
        return false;
    }
    return true;
}

// ── Hex corner geometry ───────────────────────────────────────────────────────

// All 6 corners must be at distance exactly HEX_SIZE from the center.
DELVE_TEST(hex_corners_at_correct_radius) {
    float cx, cy;
    hex_to_pixel(5, 3, Config::HEX_SIZE, cx, cy);
    Vec2 corners[6];
    get_hex_corners(5, 3, Config::HEX_SIZE, corners);
    for (int i = 0; i < 6; ++i) {
        float dx = corners[i].x - cx;
        float dy = corners[i].y - cy;
        float dist = std::sqrt(dx * dx + dy * dy);
        EXPECT_NEAR(dist, Config::HEX_SIZE, 0.01f);
    }
    return true;
}

// pixel_in_hex: center is inside, points 2× HEX_SIZE away are outside.
DELVE_TEST(pixel_in_hex_correctness_at_config_hex_size) {
    float cx, cy;
    hex_to_pixel(3, 2, Config::HEX_SIZE, cx, cy);
    // Center must be inside.
    EXPECT_TRUE(pixel_in_hex(cx, cy, 3, 2, Config::HEX_SIZE));
    // Point 2 hex-sizes away must be outside.
    EXPECT_FALSE(pixel_in_hex(cx + Config::HEX_SIZE * 2.0f, cy, 3, 2, Config::HEX_SIZE));
    EXPECT_FALSE(pixel_in_hex(cx, cy + Config::HEX_SIZE * 2.0f, 3, 2, Config::HEX_SIZE));
    return true;
}
