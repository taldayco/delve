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

DELVE_TEST(config_hex_size_large_enough) {
    EXPECT_GT(Config::HEX_SIZE, 3.9f);
    return true;
}

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

DELVE_TEST(config_lava_grid_spacing_sane) {
    EXPECT_GT(Config::LAVA_GRID_SPACING, Config::HEX_SIZE / 4.0f - 0.01f);
    return true;
}

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

DELVE_TEST(hex_to_pixel_finite_for_map_range) {
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

DELVE_TEST(pixel_to_hex_valid_for_all_map_pixels) {
    int step = std::max(1, Config::MAP_COLS / 32);
    for (int y = 0; y < Config::MAP_ROWS; y += step) {
        for (int x = 0; x < Config::MAP_COLS; x += step) {
            HexCoord h = pixel_to_hex((float)x, (float)y, Config::HEX_SIZE);
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

DELVE_TEST(hex_neighbor_spacing_correct) {
    float px0, py0, px1, py1;
    hex_to_pixel(0, 0, Config::HEX_SIZE, px0, py0);
    hex_to_pixel(1, 0, Config::HEX_SIZE, px1, py1);
    float dx = std::abs(px1 - px0);
    float expected = Config::HEX_SIZE * 1.5f;
    EXPECT_NEAR(dx, expected, 0.01f);
    return true;
}

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

DELVE_TEST(pixel_in_hex_correctness_at_config_hex_size) {
    float cx, cy;
    hex_to_pixel(3, 2, Config::HEX_SIZE, cx, cy);
    EXPECT_TRUE(pixel_in_hex(cx, cy, 3, 2, Config::HEX_SIZE));
    EXPECT_FALSE(pixel_in_hex(cx + Config::HEX_SIZE * 2.0f, cy, 3, 2, Config::HEX_SIZE));
    EXPECT_FALSE(pixel_in_hex(cx, cy + Config::HEX_SIZE * 2.0f, 3, 2, Config::HEX_SIZE));
    return true;
}
