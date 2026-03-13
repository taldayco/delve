void log_arm_swing(const RigState &anim) {
        if (!active || !file) return;
        float l_shoulder_lag = fabsf(anim.l_arm_target - anim.l_shoulder_smooth);
        float l_elbow_lag    = fabsf(anim.l_shoulder_smooth - anim.l_elbow_smooth);
        float l_wrist_lag    = fabsf(anim.l_elbow_smooth - anim.l_wrist_smooth);
        fprintf(file,
            ",\"arm_swing\":{"
            "\"l_target\":%.4f,\"r_target\":%.4f,"
            "\"l_shoulder_lag\":%.4f,\"l_elbow_lag\":%.4f,\"l_wrist_lag\":%.4f,"
            "\"r_shoulder_lag\":%.4f,\"r_elbow_lag\":%.4f,\"r_wrist_lag\":%.4f}",
            anim.l_arm_target, anim.r_arm_target,
            l_shoulder_lag, l_elbow_lag, l_wrist_lag,
            fabsf(anim.r_arm_target - anim.r_shoulder_smooth),
            fabsf(anim.r_shoulder_smooth - anim.r_elbow_smooth),
            fabsf(anim.r_elbow_smooth - anim.r_wrist_smooth));
    }

    void log_grounding(const RigState &anim, const LegState &legs) {
        if (!active || !file) return;
        fprintf(file,
            ",\"grounding\":{"
            "\"both_stepping\":%s,"
            "\"l_contact_vel\":%.4f,\"r_contact_vel\":%.4f,"
            "\"l_stepping\":%s,\"r_stepping\":%s}",
            (legs.stepping[0] && legs.stepping[1]) ? "true" : "false",
            anim.foot_contact_velocity[0], anim.foot_contact_velocity[1],
            legs.stepping[0] ? "true" : "false",
            legs.stepping[1] ? "true" : "false");
    }

private:
    FILE *file = nullptr;
    uint64_t frame_count = 0;
    glm::vec3 prev_velocity_log{0.0f};
    glm::vec3 prev_accel_log{0.0f};
};