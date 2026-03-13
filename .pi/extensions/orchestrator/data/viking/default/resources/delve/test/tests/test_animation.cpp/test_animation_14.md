EXPECT_NEAR(pose.joints[(int)Joint::HIPS].z, expected_hips, 1e-5f);
    EXPECT_NEAR(pose.joints[(int)Joint::CHEST].z, expected_chest, 1e-5f);
    EXPECT_NEAR(pose.joints[(int)Joint::HEAD].z, expected_head, 1e-5f);
    // Body joints are compressed (closer to foot_z than original)
    EXPECT_LT(pose.joints[(int)Joint::HIPS].z, hips_z);
    EXPECT_LT(pose.joints[(int)Joint::HEAD].z, head_z);
    return true;
}

DELVE_TEST(slope_foot_z_matches_terrain_after_foreshorten) {
    auto map = make_slope_map(64, 64, 0.4f);
    ActorConfig cfg;

    // Two foot positions at different world X → different terrain heights
    float wx_l = 2.0f, wx_r = 3.0f, wy = 3.0f;
    float lfoot_z = sphere_trace_height(map, wx_l, wy, cfg.leg_radius);
    float rfoot_z = sphere_trace_height(map, wx_r, wy, cfg.leg_radius);
    // Heights should differ on a slope
    EXPECT_GT(fabsf(lfoot_z - rfoot_z), 0.01f);

    RigPose pose;
    pose.joints[(int)Joint::L_FOOT].z = lfoot_z;
    pose.joints[(int)Joint::R_FOOT].z = rfoot_z;
    pose.joints[(int)Joint::L_TOE].z = lfoot_z;
    pose.joints[(int)Joint::R_TOE].z = rfoot_z;
    float avg = (lfoot_z + rfoot_z) * 0.5f;
    pose.joints[(int)Joint::HIPS].z = avg + cfg.leg_len + cfg.shin_len;
    pose.joints[(int)Joint::ROOT].z = std::max(avg, std::min(lfoot_z, rfoot_z));

    float foot_z_ref = avg;
    apply_foreshortening(pose, foot_z_ref);

    // Feet must still match their terrain heights exactly
    EXPECT_NEAR(pose.joints[(int)Joint::L_FOOT].z, lfoot_z, 1e-6f);
    EXPECT_NEAR(pose.joints[(int)Joint::R_FOOT].z, rfoot_z, 1e-6f);
    EXPECT_NEAR(pose.joints[(int)Joint::L_TOE].z, lfoot_z, 1e-6f);
    EXPECT_NEAR(pose.joints[(int)Joint::R_TOE].z, rfoot_z, 1e-6f);
    return true;
}

DELVE_TEST(root_z_survives_foreshortening) {
    ActorConfig cfg;
    RigPose pose;
    float terrain_z = 4.2f;
    float hips_z = terrain_z + cfg.leg_len + cfg.shin_len;
    float root_z = terrain_z; // clamped to terrain

    pose.joints[(int)Joint::ROOT].z = root_z;
    pose.joints[(int)Joint::HIPS].z = hips_z;
    pose.joints[(int)Joint::L_FOOT].z = terrain_z;
    pose.joints[(int)Joint::R_FOOT].z = terrain_z;

    float foot_z_ref = terrain_z;
    apply_foreshortening(pose, foot_z_ref);

    // ROOT must be unchanged
    EXPECT_NEAR(pose.joints[(int)Joint::ROOT].z, root_z, 1e-6f);
    // HIPS should be compressed
    EXPECT_LT(pose.joints[(int)Joint::HIPS].z, hips_z);
    return true;
}

// ---- Visual facing / smooth_damp_angle / half-space tests ----

// Test helper: angle-aware smooth_damp (mirrors rig_animation.cpp's static function)
static float smooth_damp_angle_test(float current, float target, float *velocity,
                                     float smooth_time, float dt) {
    float delta = target - current;
    while (delta >  glm::pi<float>()) delta -= glm::two_pi<float>();
    while (delta < -glm::pi<float>()) delta += glm::two_pi<float>();
    return smooth_damp_test(current, current + delta, velocity, smooth_time, dt);
}

DELVE_TEST(smooth_damp_angle_convergence) {
    // smooth_damp_angle should converge from 0 to π/2 within 5 seconds
    float current = 0.0f;
    float target = glm::half_pi<float>();
    float velocity = 0.0f;
    float dt = 1.0f / 60.0f;

    for (int i = 0; i < 300; ++i)  // 5 seconds at 60 fps
        current = smooth_damp_angle_test(current, target, &velocity, 0.15f, dt);

    EXPECT_NEAR(current, target, 0.01f);
    return true;
}

DELVE_TEST(smooth_damp_angle_wraps_around) {
    // From -170° to +170° should take the short path (20°), not the long path (340°)
    float from_deg = -170.0f;
    float to_deg   =  170.0f;
    float current  = glm::radians(from_deg);
    float target   = glm::radians(to_deg);
    float velocity = 0.0f;
    float dt = 1.0f / 60.0f;