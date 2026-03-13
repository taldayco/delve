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