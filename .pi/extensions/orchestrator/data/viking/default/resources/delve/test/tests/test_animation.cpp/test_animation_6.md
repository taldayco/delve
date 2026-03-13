// Test: hip_bob_y (|sin(2π*phase)|) peaks exactly twice per [0,1) cycle.
DELVE_TEST(hip_bob_two_peaks_per_cycle) {
    float prev = 0.0f, pprev = 0.0f;
    int peaks = 0;
    for (int i = 0; i <= 200; ++i) {
        float cur = test_compute_hip_counter((float)i / 200.0f).hip_bob_y;
        if (i >= 2 && pprev < prev && cur < prev)
            ++peaks;
        pprev = prev; prev = cur;
    }
    EXPECT_EQ(peaks, 2);
    return true;
}

// Test: foot Z-lift = 0 at t=0 and t=1, peaks near step_height at t=0.5.
DELVE_TEST(foot_position_z_lift_arc) {
    glm::vec3 prev{0,0,0}, target{1,0,0};
    float sh = 0.5f;
    glm::vec3 at0  = test_compute_foot_position(0.0f, prev, target, sh);
    glm::vec3 at05 = test_compute_foot_position(0.5f, prev, target, sh);
    glm::vec3 at1  = test_compute_foot_position(1.0f, prev, target, sh);

    EXPECT_NEAR(at0.z,  0.0f, 1e-4f);
    EXPECT_NEAR(at1.z,  0.0f, 1e-4f);
    EXPECT_NEAR(at05.z, sh,   0.02f);
    EXPECT_GT(at05.z, at0.z);
    EXPECT_GT(at05.z, at1.z);
    return true;
}

// Test: foot XY velocity peaks at mid-stride (sine-warp easing).
DELVE_TEST(foot_position_xy_velocity_peaks_at_midstride) {
    glm::vec3 prev{0,0,0}, target{2,0,0};
    float dp = 0.05f;
    float v_start = test_compute_foot_position(dp, prev, target, 0.0f).x
                  - test_compute_foot_position(0.0f, prev, target, 0.0f).x;
    float v_mid   = test_compute_foot_position(0.5f + dp, prev, target, 0.0f).x
                  - test_compute_foot_position(0.5f, prev, target, 0.0f).x;
    float v_end   = test_compute_foot_position(1.0f, prev, target, 0.0f).x
                  - test_compute_foot_position(1.0f - dp, prev, target, 0.0f).x;

    EXPECT_GT(v_mid, v_start);
    EXPECT_GT(v_mid, v_end);
    EXPECT_LT(v_start, v_mid * 0.5f);
    return true;
}

// Test: foot position XY reaches target exactly at t=1 (Z also reaches target when sh=0).
DELVE_TEST(foot_position_reaches_target_at_t1) {
    glm::vec3 prev{3,-1,2}, target{5,2,1};
    glm::vec3 at1 = test_compute_foot_position(1.0f, prev, target, 0.0f);
    EXPECT_NEAR(at1.x, target.x, 1e-4f);
    EXPECT_NEAR(at1.y, target.y, 1e-4f);
    EXPECT_NEAR(at1.z, target.z, 1e-4f);
    return true;
}

// ---- ISO height foreshortening tests ----

// Test: AnimationConfig::ISO_CHAR_HEIGHT_SCALE = 0.816 * 0.92 ≈ 0.751.
// This constant squashes character height for correct 2:1 isometric proportions.
DELVE_TEST(iso_char_height_scale_value) {
    float scale = AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
    // Should be ~0.751 (within 1% tolerance).
    EXPECT_GT(scale, 0.74f);
    EXPECT_LT(scale, 0.76f);
    return true;
}

// Test: Foreshortening reduces skeleton height relative to foot_z.
// After applying ISO_CHAR_HEIGHT_SCALE, the vertical extent above foot_z
// is strictly less than before (and greater than 0).
DELVE_TEST(iso_foreshortening_reduces_height) {
    ActorConfig cfg;
    float foot_z = 0.0f;

    // Build a simple spine stack above foot_z.
    float heights_before[5] = {
        0.0f,
        cfg.leg_len,
        cfg.leg_len + cfg.shin_len,
        cfg.leg_len + cfg.shin_len + cfg.torso_len,
        cfg.leg_len + cfg.shin_len + cfg.torso_len + cfg.neck_len + cfg.head_radius
    };

    float scale = AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
    float top_before = heights_before[4];
    float top_after  = foot_z + (top_before - foot_z) * scale;

    EXPECT_LT(top_after, top_before);
    EXPECT_GT(top_after, 0.0f);
    // Foot level stays planted (foot_z is the reference, not scaled).
    float foot_after = foot_z + (foot_z - foot_z) * scale;
    EXPECT_NEAR(foot_after, foot_z, 1e-6f);
    return true;
}

// Test: Planted-foot XY clamp limits horizontal distance from hip.
// Simulates a foot planted far behind its hip (as in a 180° turn).
DELVE_TEST(planted_foot_clamped_to_stride_len) {
  ProceduralGait gait{};
  float max_horiz = gait.stride_len * 0.9f;

  // Foot 3.0 units behind hip — clearly hyperextended.
  float hip_x = 5.0f, hip_y = 0.0f;
  float foot_x = 2.0f, foot_y = 0.0f;

  float dx = foot_x - hip_x;
  float dy = foot_y - hip_y;
  float hd = sqrtf(dx * dx + dy * dy);
  EXPECT_GT(hd, max_horiz); // confirm over the limit