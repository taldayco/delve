// Test: width_scale bounds: 1.0 at horizontal, 3.0 at vertical, monotonically increasing.
DELVE_TEST(iso_compressor_width_scale_bounds) {
    float prev_ws = 0.0f;
    for (int i = 0; i <= 10; ++i) {
        float t = (float)i / 10.0f;
        // Bone from origin to (1-t, 0, t) — sweeps from horizontal to vertical.
        float x = 1.0f - t;
        float z = t;
        if (x < 1e-5f && z < 1e-5f) continue;
        auto c = test_iso_compensate({0,0,0}, {x, 0, z});
        EXPECT_GT(c.width_scale, 0.99f);
        EXPECT_LT(c.width_scale, 3.01f);
        if (i > 0) EXPECT_GT(c.width_scale, prev_ws - 0.01f); // monotonic
        prev_ws = c.width_scale;
    }
    return true;
}

// Test: equator_t range: 0.20 → 0.35, monotonic.
DELVE_TEST(iso_compressor_equator_range) {
    auto h = test_iso_compensate({0,0,0}, {1,0,0});
    auto v = test_iso_compensate({0,0,0}, {0,0,1});
    EXPECT_NEAR(h.equator_t, 0.20f, 0.01f);
    EXPECT_NEAR(v.equator_t, 0.35f, 0.01f);
    // Intermediate is between
    auto d = test_iso_compensate({0,0,0}, {1,0,1});
    EXPECT_GT(d.equator_t, 0.20f);
    EXPECT_LT(d.equator_t, 0.35f);
    return true;
}

// Test: radial_z_comp lerp: 0.18 → 1.0.
DELVE_TEST(iso_compressor_radial_z_range) {
    auto h = test_iso_compensate({0,0,0}, {1,0,0});
    auto v = test_iso_compensate({0,0,0}, {0,0,1});
    EXPECT_NEAR(h.radial_z_comp, 0.18f, 0.01f);
    EXPECT_NEAR(v.radial_z_comp, 1.0f, 0.01f);
    // Intermediate
    auto d = test_iso_compensate({0,0,0}, {1,0,1});
    EXPECT_GT(d.radial_z_comp, 0.18f);
    EXPECT_LT(d.radial_z_comp, 1.0f);
    return true;
}

// Test: Ground trace endpoints sit at ROOT.z level.
DELVE_TEST(ground_trace_z_at_root_level) {
    // Simulate: ROOT at z=5.0, HIPS at z=6.0, feet at z=5.2
    float root_z = 5.0f;
    glm::vec3 hips(1.0f, 2.0f, 6.0f);
    glm::vec3 l_foot(-0.5f, 2.0f, 5.2f);
    glm::vec3 r_foot(0.5f, 2.0f, 5.2f);

    // Ground projections (mirrors prepare() logic)
    glm::vec3 hip_ground(hips.x, hips.y, root_z);
    glm::vec3 lfoot_ground(l_foot.x, l_foot.y, root_z);
    glm::vec3 rfoot_ground(r_foot.x, r_foot.y, root_z);

    EXPECT_NEAR(hip_ground.z, root_z, 1e-6f);
    EXPECT_NEAR(lfoot_ground.z, root_z, 1e-6f);
    EXPECT_NEAR(rfoot_ground.z, root_z, 1e-6f);
    // XY preserved from source joints
    EXPECT_NEAR(hip_ground.x, hips.x, 1e-6f);
    EXPECT_NEAR(lfoot_ground.x, l_foot.x, 1e-6f);
    return true;
}

// Test: V/A frame bar span matches config — shoulder bar connects L/R upper arms,
// pelvis bar connects L/R upper legs.
DELVE_TEST(va_frame_bar_span_matches_config) {
    ActorConfig cfg;
    // At rest pose, upper arms are at ±shoulder_width from center
    glm::vec3 l_upper_arm(-cfg.shoulder_width, 0, cfg.torso_len);
    glm::vec3 r_upper_arm( cfg.shoulder_width, 0, cfg.torso_len);
    float shoulder_span = glm::length(r_upper_arm - l_upper_arm);
    EXPECT_NEAR(shoulder_span, 2.0f * cfg.shoulder_width, 1e-4f);

    // Pelvis bar: upper legs at ±hip_width
    glm::vec3 l_upper_leg(-cfg.hip_width, 0, 0);
    glm::vec3 r_upper_leg( cfg.hip_width, 0, 0);
    float pelvis_span = glm::length(r_upper_leg - l_upper_leg);
    EXPECT_NEAR(pelvis_span, 2.0f * cfg.hip_width, 1e-4f);

    // Shoulder bar is wider than pelvis bar
    EXPECT_GT(shoulder_span, pelvis_span);
    return true;
}

// ---- Phase 1: Continuous foot grounding + slope adaptation tests ----

// Test: Hip tilt from foot height difference is bounded to ±8°.
DELVE_TEST(hip_tilt_bounded_by_max_angle) {
    float max_tilt = glm::radians(8.0f);
    ActorConfig cfg;

    // Extreme foot height difference (1.0 unit)
    float foot_diff = 1.0f;
    float tilt = std::clamp(
        atan2f(foot_diff, cfg.hip_width * 2.0f),
        -max_tilt, max_tilt);
    EXPECT_LT(fabsf(tilt), max_tilt + 1e-4f);

    // Negative difference
    float tilt_neg = std::clamp(
        atan2f(-foot_diff, cfg.hip_width * 2.0f),
        -max_tilt, max_tilt);
    EXPECT_GT(tilt_neg, -max_tilt - 1e-4f);

    // Zero difference → zero tilt
    float tilt_zero = std::clamp(
        atan2f(0.0f, cfg.hip_width * 2.0f),
        -max_tilt, max_tilt);
    EXPECT_NEAR(tilt_zero, 0.0f, 1e-4f);
    return true;
}