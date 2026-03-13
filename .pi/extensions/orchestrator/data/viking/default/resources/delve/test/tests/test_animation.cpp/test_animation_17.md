jt[(int)J::R_UPPER_LEG] = {cfg.hip_width, 0, 0};
    jt[(int)J::R_LOWER_LEG] = {cfg.hip_width, 0, -cfg.leg_len};
    jt[(int)J::R_FOOT]      = {cfg.hip_width, 0, -cfg.leg_len - cfg.shin_len};
    jt[(int)J::R_TOE]       = {cfg.hip_width, cfg.toe_len, -cfg.leg_len - cfg.shin_len};

    // IK virtual joints (just place at end-effectors)
    jt[(int)J::IK_FOOT_L]   = jt[(int)J::L_FOOT];
    jt[(int)J::IK_FOOT_R]   = jt[(int)J::R_FOOT];
    jt[(int)J::IK_HAND_L]   = jt[(int)J::L_HAND];
    jt[(int)J::IK_HAND_R]   = jt[(int)J::R_HAND];
    jt[(int)J::POLE_KNEE_L]  = jt[(int)J::L_LOWER_LEG] + glm::vec3(0, 0.25f, 0);
    jt[(int)J::POLE_KNEE_R]  = jt[(int)J::R_LOWER_LEG] + glm::vec3(0, 0.25f, 0);
    jt[(int)J::POLE_ELBOW_L] = jt[(int)J::L_LOWER_ARM] + glm::vec3(0, -0.25f, 0);
    jt[(int)J::POLE_ELBOW_R] = jt[(int)J::R_LOWER_ARM] + glm::vec3(0, -0.25f, 0);
    return pose;
}

DELVE_TEST(rig_transforms_default_zero_initialized) {
    RigTransforms xf;
    for (int i = 0; i < (int)Joint::COUNT; ++i) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                EXPECT_NEAR(xf.bones[i][c][r], 0.0f, 1e-6f);
            }
        }
    }
    return true;
}

DELVE_TEST(rig_transforms_build_bone_basis_degenerate) {
    glm::vec3 right, fwd, up;

    // Zero-length input → identity basis
    build_bone_basis(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), right, fwd, up);
    EXPECT_NEAR(right.x, 1.0f, 1e-5f);
    EXPECT_NEAR(fwd.y,   1.0f, 1e-5f);
    EXPECT_NEAR(up.z,    1.0f, 1e-5f);

    // Parallel input (bone_dir == ref_fwd) → still valid orthonormal
    build_bone_basis(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                     right, fwd, up);
    float dot_ru = glm::dot(right, up);
    float dot_rf = glm::dot(right, fwd);
    float dot_fu = glm::dot(fwd, up);
    EXPECT_NEAR(dot_ru, 0.0f, 1e-4f);
    EXPECT_NEAR(dot_rf, 0.0f, 1e-4f);
    EXPECT_NEAR(dot_fu, 0.0f, 1e-4f);
    EXPECT_NEAR(glm::length(right), 1.0f, 1e-4f);
    EXPECT_NEAR(glm::length(fwd),   1.0f, 1e-4f);
    EXPECT_NEAR(glm::length(up),    1.0f, 1e-4f);

    // Standard input → valid orthonormal right-handed
    build_bone_basis(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                     right, fwd, up);
    EXPECT_NEAR(glm::length(right), 1.0f, 1e-4f);
    EXPECT_NEAR(glm::length(fwd),   1.0f, 1e-4f);
    EXPECT_NEAR(glm::length(up),    1.0f, 1e-4f);
    // Right-handed: cross(right, fwd) ≈ up
    glm::vec3 check = glm::cross(right, fwd);
    EXPECT_NEAR(check.x, up.x, 1e-4f);
    EXPECT_NEAR(check.y, up.y, 1e-4f);
    EXPECT_NEAR(check.z, up.z, 1e-4f);
    return true;
}