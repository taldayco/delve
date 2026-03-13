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