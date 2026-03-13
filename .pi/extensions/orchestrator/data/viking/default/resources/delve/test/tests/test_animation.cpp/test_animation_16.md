// Half-space corrective step
        for (int leg = 0; leg < 2; ++leg) {
            if (legs.stepping[leg] || legs.stepping[1 - leg]) continue;
            float lat = (legs.foot[leg].x - pos_x) * vf_rght_x;
            bool crossed = (hip_sign[leg] < 0) ? (lat > 0.02f) : (lat < -0.02f);
            if (crossed) {
                legs.stepping[leg] = true;
                legs.progress[leg] = 0.0f;
                legs.prev_foot[leg] = legs.foot[leg];
                legs.target[leg] = {pos_x + vf_rght_x * hip_sign[leg] * cfg.hip_width, 0.0f, 0.0f};
            }
        }

        // Check: no foot on wrong side when both feet are planted
        // (during stepping, one foot may temporarily be on wrong side)
        if (!legs.stepping[0] && !legs.stepping[1]) {
            for (int leg = 0; leg < 2; ++leg) {
                float lat = (legs.foot[leg].x - pos_x) * vf_rght_x;
                bool wrong_side = (hip_sign[leg] < 0) ? (lat > 0.05f) : (lat < -0.05f);
                if (wrong_side)
                    any_crossed = true;
            }
        }
    }

    EXPECT_FALSE(any_crossed);
    return true;
}

DELVE_TEST(half_space_corrective_step_triggers) {
    // Left foot at y=+0.3 when visual_facing=0 (facing +X): rght = (0, 1)
    // Left hip_sign = -1, so left side is y < 0.
    // lat = (foot.y - center.y) * rght_y = (0.3 - 0) * 1.0 = 0.3
    // crossed = (hip_sign < 0) ? (lat > 0.02) = true
    float vf_rght_x = 0.0f, vf_rght_y = 1.0f;  // facing = 0 → right = (0, 1)
    float pos_x = 0.0f, pos_y = 0.0f;

    glm::vec3 left_foot(0.0f, 0.3f, 0.0f);  // on the wrong side
    float hip_sign_left = -1.0f;

    float lat = (left_foot.x - pos_x) * vf_rght_x + (left_foot.y - pos_y) * vf_rght_y;
    bool crossed = (hip_sign_left < 0) ? (lat > 0.02f) : (lat < -0.02f);

    EXPECT_TRUE(crossed);
    return true;
}

DELVE_TEST(foot_target_clamped_to_correct_side) {
    // Target on wrong side should be projected back
    float vf_rght_x = 0.0f, vf_rght_y = 1.0f;  // facing = 0
    float pos_x = 0.0f, pos_y = 0.0f;
    float hip_sign_left = -1.0f;

    // Target placed at y=+0.2 (wrong side for left leg)
    float tgt_x = 0.5f, tgt_y = 0.2f;
    float tgt_lat = (tgt_x - pos_x) * vf_rght_x + (tgt_y - pos_y) * vf_rght_y;

    if ((hip_sign_left < 0 && tgt_lat > 0) || (hip_sign_left > 0 && tgt_lat < 0)) {
        tgt_x -= vf_rght_x * tgt_lat - vf_rght_x * hip_sign_left * 0.05f;
        tgt_y -= vf_rght_y * tgt_lat - vf_rght_y * hip_sign_left * 0.05f;
    }

    // After clamping, lat should be on correct side (≤ 0 for left leg)
    float new_lat = (tgt_x - pos_x) * vf_rght_x + (tgt_y - pos_y) * vf_rght_y;
    EXPECT_LT(new_lat, 0.0f);
    return true;
}

// ---- RigTransforms tests ----

// Helper: build a complete T-pose RigPose for transform tests.
static RigPose make_tpose() {
    RigPose pose;
    ActorConfig cfg;
    using J = Joint;
    auto &jt = pose.joints;
    float ground_z = -(cfg.leg_len + cfg.shin_len);
    jt[(int)J::ROOT]       = {0, 0, ground_z};
    jt[(int)J::HIPS]       = {0, 0, 0};
    jt[(int)J::SPINE_01]   = {0, 0, cfg.torso_len * 0.3f};
    jt[(int)J::SPINE_02]   = {0, 0, cfg.torso_len * 0.6f};
    jt[(int)J::CHEST]      = {0, 0, cfg.torso_len};
    jt[(int)J::NECK]       = {0, 0, cfg.torso_len + cfg.neck_len * 0.5f};
    jt[(int)J::HEAD]       = {0, 0, cfg.torso_len + cfg.neck_len};
    jt[(int)J::HEAD_END]   = {0, 0, cfg.torso_len + cfg.neck_len + cfg.head_radius};

    jt[(int)J::L_CLAVICLE]  = {-cfg.shoulder_width * 0.25f, 0, cfg.torso_len};
    jt[(int)J::L_UPPER_ARM] = {-cfg.shoulder_width, 0, cfg.torso_len};
    jt[(int)J::L_LOWER_ARM] = {-cfg.shoulder_width - cfg.arm_len, 0, cfg.torso_len};
    jt[(int)J::L_HAND]      = {-cfg.shoulder_width - cfg.arm_len - cfg.forearm_len, 0, cfg.torso_len};

    jt[(int)J::R_CLAVICLE]  = {cfg.shoulder_width * 0.25f, 0, cfg.torso_len};
    jt[(int)J::R_UPPER_ARM] = {cfg.shoulder_width, 0, cfg.torso_len};
    jt[(int)J::R_LOWER_ARM] = {cfg.shoulder_width + cfg.arm_len, 0, cfg.torso_len};
    jt[(int)J::R_HAND]      = {cfg.shoulder_width + cfg.arm_len + cfg.forearm_len, 0, cfg.torso_len};

    jt[(int)J::L_UPPER_LEG] = {-cfg.hip_width, 0, 0};
    jt[(int)J::L_LOWER_LEG] = {-cfg.hip_width, 0, -cfg.leg_len};
    jt[(int)J::L_FOOT]      = {-cfg.hip_width, 0, -cfg.leg_len - cfg.shin_len};
    jt[(int)J::L_TOE]       = {-cfg.hip_width, cfg.toe_len, -cfg.leg_len - cfg.shin_len};