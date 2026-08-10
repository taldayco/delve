#include "test_harness.h"
#include "config.h"
#include "camera/camera.h"
#include "terrain/hex.h"
#include "terrain/map_data.h"
#include "terrain/map_util.h"
#include "terrain/noise_layers.h"
#include "terrain/noise_composer.h"
#include "terrain/basalt.h"
#include "terrain/lava.h"
#include "terrain/contour.h"
#include <cmath>
#include <vector>

DELVE_TEST(config_hex_size_large_enough) {
    EXPECT_GT(Config::HEX_SIZE, 3.9f);
    return true;
}

DELVE_TEST(config_map_width_units_cluster_safe) {
    float map_width_units = (float)Config::MAP_WIDTH / Config::HEX_SIZE;
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
    int q_max = (int)(Config::MAP_WIDTH / Config::HEX_SIZE) + 5;
    int r_max = (int)(Config::MAP_HEIGHT / Config::HEX_SIZE) + 5;
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
    int step = std::max(1, Config::MAP_WIDTH / 32);
    for (int y = 0; y < Config::MAP_HEIGHT; y += step) {
        for (int x = 0; x < Config::MAP_WIDTH; x += step) {
            HexCoord h = pixel_to_hex((float)x, (float)y, Config::HEX_SIZE);
            int range = Config::MAP_WIDTH + Config::MAP_HEIGHT;
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

DELVE_TEST(camera_view_matrix_iso_constants) {
    CameraSystem sys;
    CameraState cam;
    cam.world_x = 0.0f;
    cam.world_y = 0.0f;
    cam.follow_z = 0.0f;
    auto mats = sys.build_matrices(cam, 1.0f);
    auto &v = mats.view;
    EXPECT_NEAR(v[0][0], Config::ISO_TW, 1e-5f);
    EXPECT_NEAR(v[0][1], Config::ISO_TH, 1e-5f);
    EXPECT_NEAR(v[1][0], -Config::ISO_TW, 1e-5f);
    EXPECT_NEAR(v[1][1], Config::ISO_TH, 1e-5f);
    EXPECT_NEAR(v[2][1], -Config::ISO_HS, 1e-5f);
    return true;
}

DELVE_TEST(camera_world_origin_maps_to_clip_center) {
    CameraSystem sys;
    CameraState cam;
    cam.world_x = 0.0f;
    cam.world_y = 0.0f;
    cam.follow_z = 0.0f;
    auto mats = sys.build_matrices(cam, 1.0f);
    glm::vec4 origin(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 view_pos = mats.view * origin;
    glm::vec4 clip = mats.projection * view_pos;
    if (std::abs(clip.w) > 1e-6f) {
        float ndc_x = clip.x / clip.w;
        float ndc_y = clip.y / clip.w;
        EXPECT_NEAR(ndc_x, 0.0f, 0.1f);
        EXPECT_NEAR(ndc_y, 0.0f, 0.1f);
    }
    return true;
}

DELVE_TEST(camera_z_height_foreshortening) {
    CameraSystem sys;
    CameraState cam;
    cam.world_x = 0.0f;
    cam.world_y = 0.0f;
    cam.follow_z = 0.0f;
    auto mats = sys.build_matrices(cam, 1.0f);
    glm::vec4 low(5.0f, 5.0f, 0.0f, 1.0f);
    glm::vec4 high(5.0f, 5.0f, 1.0f, 1.0f);
    glm::vec4 v_low = mats.view * low;
    glm::vec4 v_high = mats.view * high;
    float screen_y_diff = v_high.y - v_low.y;
    EXPECT_NEAR(screen_y_diff, -Config::ISO_HS, 0.01f);
    return true;
}

DELVE_TEST(camera_near_far_encompass_terrain) {

    float worst_view_z = 2.0f * Config::MAP_WIDTH_UNITS + 2.0f;
    CameraState cam;
    EXPECT_GE(-cam.near_plane, worst_view_z);
    EXPECT_GE(0.0f, -cam.far_plane - 0.001f);
    return true;
}

DELVE_TEST(spawn_height_sample_roundtrip) {
    static constexpr int W = 256, H = 256;
    MapData md;
    md.allocate(W, H);
    ElevationParams elev; elev.seed = 42;
    RiverParams river;    river.seed = 43;
    WorleyParams worley;  worley.seed = 44;
    CompositionParams comp;
    compose_layers(md, elev, river, worley, comp, nullptr);
    md.columns = generate_basalt_columns_v2(md, Config::HEX_SIZE);
    auto fill = generate_lava_and_void(md, comp.void_chance, worley.seed);
    md.lava_bodies = std::move(fill.lava_bodies);
    md.void_bodies = std::move(fill.void_bodies);

    if (md.columns.empty()) return true;

    HexColumn spawn = md.columns[md.columns.size() / 2];
    float px, py;
    hex_to_pixel(spawn.q, spawn.r, Config::HEX_SIZE, px, py);
    float wx = px / Config::HEX_SIZE;
    float wy = py / Config::HEX_SIZE;

    float sampled_h = sample_world_height(md, wx, wy);
    int ix = std::max(0, std::min((int)(px), W - 1));
    int iy = std::max(0, std::min((int)(py), H - 1));
    float direct_h = md.basalt_height[iy * W + ix];
    EXPECT_NEAR(sampled_h, direct_h, 0.5f);
    return true;
}
