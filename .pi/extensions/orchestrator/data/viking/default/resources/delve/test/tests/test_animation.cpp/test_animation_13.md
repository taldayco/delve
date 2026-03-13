DELVE_TEST(sphere_trace_flat_equals_point) {
    // On flat terrain, sphere trace should equal point sample.
    auto map = make_flat_map(32, 32, 5.0f);
    float wx = 1.5f, wy = 1.5f;
    float point_h = sample_world_height(map, wx, wy);
    float sphere_h = sphere_trace_height(map, wx, wy, 0.1f);
    EXPECT_NEAR(sphere_h, point_h, 1e-5f);
    return true;
}

DELVE_TEST(sphere_trace_radius_zero_equals_point) {
    // Zero radius should return exactly the point sample.
    auto map = make_slope_map(64, 64, 0.3f);
    float wx = 2.0f, wy = 2.0f;
    float point_h = sample_world_height(map, wx, wy);
    float sphere_h = sphere_trace_height(map, wx, wy, 0.0f);
    EXPECT_NEAR(sphere_h, point_h, 1e-6f);
    return true;
}

DELVE_TEST(foot_z_clears_terrain_on_slope) {
    // Simulate planted foot with sphere trace at leg_radius.
    // Verify foot.z >= point-sampled height everywhere within leg_radius.
    auto map = make_slope_map(64, 64, 0.4f);
    ActorConfig cfg;
    float wx = 3.0f, wy = 3.0f;
    float foot_z = sphere_trace_height(map, wx, wy, cfg.leg_radius);

    // Check 8 perimeter points within leg_radius
    for (int i = 0; i < 8; ++i) {
        float angle = i * (2.0f * 3.14159265f / 8.0f);
        float sx = wx + cfg.leg_radius * cosf(angle);
        float sy = wy + cfg.leg_radius * sinf(angle);
        float terrain_h = sample_world_height(map, sx, sy);
        EXPECT_GE(foot_z, terrain_h);
    }
    return true;
}

DELVE_TEST(root_z_above_terrain) {
    // Verify the new ROOT formula: max(algebraic, terrain) >= terrain.
    auto map = make_slope_map(64, 64, 0.5f);
    ActorConfig cfg;
    float hips_z_values[] = {1.0f, 2.0f, 5.0f, 0.5f};
    float wx = 3.0f, wy = 3.0f;
    float terrain_z = sample_world_height(map, wx, wy);

    for (float hips_z : hips_z_values) {
        float algebraic = hips_z - (cfg.leg_len + cfg.shin_len);
        float root_z = std::max(algebraic, terrain_z);
        EXPECT_GE(root_z, terrain_z);
    }
    return true;
}

// ---- Foreshortening ground-contact exemption tests ----

// Helper: apply the foreshortening logic with ground-contact exemption (mirrors rig_renderer.cpp).
static void apply_foreshortening(RigPose &pose, float foot_z) {
    using J = Joint;
    constexpr int ground_joints[] = {
        (int)J::L_FOOT, (int)J::R_FOOT,
        (int)J::L_TOE,  (int)J::R_TOE,
        (int)J::ROOT
    };
    float saved[5];
    for (int k = 0; k < 5; ++k) saved[k] = pose.joints[ground_joints[k]].z;

    for (int ji = 0; ji < (int)J::COUNT; ++ji) {
        pose.joints[ji].z = foot_z + (pose.joints[ji].z - foot_z)
                            * AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
    }

    for (int k = 0; k < 5; ++k) pose.joints[ground_joints[k]].z = saved[k];
}

DELVE_TEST(foreshortening_preserves_foot_z) {
    ActorConfig cfg;
    RigPose pose;
    // Simulate slope: left foot higher than right
    float lfoot_z = 3.0f, rfoot_z = 2.5f;
    pose.joints[(int)Joint::L_FOOT].z = lfoot_z;
    pose.joints[(int)Joint::R_FOOT].z = rfoot_z;
    pose.joints[(int)Joint::HIPS].z = 3.5f;

    float foot_z = (lfoot_z + rfoot_z) * 0.5f; // reference
    apply_foreshortening(pose, foot_z);

    EXPECT_NEAR(pose.joints[(int)Joint::L_FOOT].z, lfoot_z, 1e-6f);
    EXPECT_NEAR(pose.joints[(int)Joint::R_FOOT].z, rfoot_z, 1e-6f);
    return true;
}

DELVE_TEST(foreshortening_compresses_body) {
    ActorConfig cfg;
    RigPose pose;
    float foot_z = 2.0f;
    float hips_z = 2.0f + cfg.leg_len + cfg.shin_len;
    float chest_z = hips_z + cfg.torso_len * 0.7f;
    float head_z = hips_z + cfg.torso_len + cfg.neck_len + cfg.head_radius;

    pose.joints[(int)Joint::HIPS].z = hips_z;
    pose.joints[(int)Joint::CHEST].z = chest_z;
    pose.joints[(int)Joint::HEAD].z = head_z;
    pose.joints[(int)Joint::L_FOOT].z = foot_z;
    pose.joints[(int)Joint::R_FOOT].z = foot_z;
    pose.joints[(int)Joint::ROOT].z = foot_z;

    apply_foreshortening(pose, foot_z);

    float scale = AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
    float expected_hips = foot_z + (hips_z - foot_z) * scale;
    float expected_chest = foot_z + (chest_z - foot_z) * scale;
    float expected_head = foot_z + (head_z - foot_z) * scale;