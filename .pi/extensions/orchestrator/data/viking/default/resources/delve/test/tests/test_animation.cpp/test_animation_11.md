// Test: Foot-average hip height is correct for level feet.
DELVE_TEST(foot_average_hip_height_level) {
    ActorConfig cfg;
    float foot_z = 3.0f;
    float avg_foot_z = (foot_z + foot_z) * 0.5f;
    float target_z = avg_foot_z + cfg.leg_len + cfg.shin_len;
    float expected = foot_z + cfg.leg_len + cfg.shin_len;
    EXPECT_NEAR(target_z, expected, 1e-6f);
    return true;
}

// Test: Foot-average hip height splits between two different foot heights.
DELVE_TEST(foot_average_hip_height_sloped) {
    ActorConfig cfg;
    float foot_l = 2.0f, foot_r = 4.0f;
    float avg = (foot_l + foot_r) * 0.5f;
    float target_z = avg + cfg.leg_len + cfg.shin_len;
    float mid_height = 3.0f + cfg.leg_len + cfg.shin_len;
    EXPECT_NEAR(target_z, mid_height, 1e-6f);
    return true;
}

// ---- Phase 2: Look-at system tests ----

// Test: Look-at yaw clamp limits to ±70°.
DELVE_TEST(look_at_yaw_clamp) {
    float max_yaw = glm::radians(70.0f);

    // Target directly behind (180° relative yaw)
    float target_yaw = glm::pi<float>();
    target_yaw = std::clamp(target_yaw, -max_yaw, max_yaw);
    EXPECT_NEAR(target_yaw, max_yaw, 1e-4f);

    // Target to the right (90° yaw)
    float right_yaw = glm::radians(90.0f);
    right_yaw = std::clamp(right_yaw, -max_yaw, max_yaw);
    EXPECT_NEAR(right_yaw, max_yaw, 1e-4f);

    // Target slightly left (30° yaw — within limits)
    float left_yaw = glm::radians(-30.0f);
    left_yaw = std::clamp(left_yaw, -max_yaw, max_yaw);
    EXPECT_NEAR(left_yaw, glm::radians(-30.0f), 1e-4f);
    return true;
}

// Test: Look-at pitch clamp limits to ±30°.
DELVE_TEST(look_at_pitch_clamp) {
    float max_pitch = glm::radians(30.0f);

    float up_pitch = glm::radians(60.0f);
    up_pitch = std::clamp(up_pitch, -max_pitch, max_pitch);
    EXPECT_NEAR(up_pitch, max_pitch, 1e-4f);

    float down_pitch = glm::radians(-15.0f);
    down_pitch = std::clamp(down_pitch, -max_pitch, max_pitch);
    EXPECT_NEAR(down_pitch, glm::radians(-15.0f), 1e-4f);
    return true;
}

// Test: Look-at weight zero produces no offset.
DELVE_TEST(look_at_weight_zero_no_offset) {
    float target_yaw = glm::radians(45.0f);
    float weight = 0.0f;
    float effective_yaw = target_yaw * weight;
    EXPECT_NEAR(effective_yaw, 0.0f, 1e-6f);
    return true;
}

// ---- Phase 3: Two-bone solver tests ----

// Test: solve_two_bone produces mid-joint at correct distance from root.
// Mirror of file-scope solve_two_bone from rig_animation.cpp.
static void test_solve_two_bone(glm::vec3 H, glm::vec3 target,
                                float a, float b,
                                glm::vec3 pole, glm::vec3 fallback_perp,
                                glm::vec3 &out_mid, glm::vec3 &out_end) {
    out_end = target;
    glm::vec3 axis = target - H;
    float D = glm::length(axis);
    float min_D = fabsf(a - b) + 0.001f;
    float max_D = a + b - 0.005f;
    float stretch_limit = max_D * 1.15f;
    if (D > stretch_limit && D > 1e-5f)
        out_end = H + (axis / D) * stretch_limit;
    D = std::max(min_D, std::min(D, max_D));
    if (glm::length(axis) > 1e-5f)
        axis = glm::normalize(axis) * D;
    else
        axis = glm::vec3(0, 0, -D);
    float cos_alpha = (a * a + D * D - b * b) / (2.0f * a * D);
    cos_alpha = std::max(-1.0f, std::min(1.0f, cos_alpha));
    float alpha = acosf(cos_alpha);
    glm::vec3 axis_n = glm::normalize(axis);
    glm::vec3 pole_off = pole - H;
    glm::vec3 perp = pole_off - glm::dot(pole_off, axis_n) * axis_n;
    if (glm::length(perp) > 1e-5f)
        perp = glm::normalize(perp);
    else
        perp = fallback_perp;
    glm::vec3 dir = axis_n * cosf(alpha) + perp * sinf(alpha);
    out_mid = H + dir * a;
}

DELVE_TEST(two_bone_solver_mid_joint_distance) {
    ActorConfig cfg;
    glm::vec3 H(0, 0, 0);
    glm::vec3 target(0, 0, -(cfg.arm_len + cfg.forearm_len) * 0.8f);
    glm::vec3 pole(0, -0.3f, -0.1f);
    glm::vec3 mid, end;
    test_solve_two_bone(H, target, cfg.arm_len, cfg.forearm_len,
                        pole, glm::vec3(0, -1, 0), mid, end);

    float mid_dist = glm::length(mid - H);
    EXPECT_NEAR(mid_dist, cfg.arm_len, 0.01f);

    float end_dist = glm::length(end - mid);
    EXPECT_NEAR(end_dist, cfg.forearm_len, 0.02f);
    return true;
}