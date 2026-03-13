using J = Joint;
        auto rel = [&](Joint child, Joint parent) {
            auto &c = pose.joints[(int)child];
            auto &p = pose.joints[(int)parent];
            return glm::vec3(c.x - p.x, c.y - p.y, c.z - p.z);
        };

        float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);

        auto l_elbow_off = rel(J::L_LOWER_ARM, J::L_UPPER_ARM);
        auto r_elbow_off = rel(J::R_LOWER_ARM, J::R_UPPER_ARM);
        float l_arm_swing = l_elbow_off.x * fwd_x + l_elbow_off.y * fwd_y;
        float r_arm_swing = r_elbow_off.x * fwd_x + r_elbow_off.y * fwd_y;

        auto l_shoulder_off = rel(J::L_UPPER_ARM, J::CHEST);
        auto r_shoulder_off = rel(J::R_UPPER_ARM, J::CHEST);

        float root_offset_x = pose.joints[(int)J::ROOT].x - t.x;
        float root_offset_y = pose.joints[(int)J::ROOT].y - t.y;

        fprintf(file,
            ",\"arm_diagnostics\":{"
            "\"l_elbow_rel_shoulder\":[%.4f,%.4f,%.4f],"
            "\"r_elbow_rel_shoulder\":[%.4f,%.4f,%.4f],"
            "\"l_arm_swing_fwd\":%.4f,"
            "\"r_arm_swing_fwd\":%.4f,"
            "\"l_shoulder_rel_chest\":[%.4f,%.4f,%.4f],"
            "\"r_shoulder_rel_chest\":[%.4f,%.4f,%.4f],"
            "\"arms_identical\":%s}",
            l_elbow_off.x, l_elbow_off.y, l_elbow_off.z,
            r_elbow_off.x, r_elbow_off.y, r_elbow_off.z,
            l_arm_swing, r_arm_swing,
            l_shoulder_off.x, l_shoulder_off.y, l_shoulder_off.z,
            r_shoulder_off.x, r_shoulder_off.y, r_shoulder_off.z,
            (l_arm_swing == r_arm_swing) ? "true" : "false");

        auto spine_off = rel(J::SPINE_01, J::HIPS);
        auto chest_off = rel(J::CHEST, J::SPINE_01);
        auto neck_off  = rel(J::NECK, J::CHEST);
        fprintf(file,
            ",\"spine_diagnostics\":{"
            "\"root_sway_offset\":[%.4f,%.4f],"
            "\"spine_rel_root\":[%.4f,%.4f,%.4f],"
            "\"chest_rel_spine\":[%.4f,%.4f,%.4f],"
            "\"neck_rel_chest\":[%.4f,%.4f,%.4f]}",
            root_offset_x, root_offset_y,
            spine_off.x, spine_off.y, spine_off.z,
            chest_off.x, chest_off.y, chest_off.z,
            neck_off.x, neck_off.y, neck_off.z);
    }

    void log_camera(const CameraState &cam) {
        if (!active || !file) return;
        fprintf(file,
            ",\"camera\":{\"world_x\":%.4f,\"world_y\":%.4f,\"zoom\":%.4f,\"following\":%s}",
            cam.world_x, cam.world_y, cam.zoom, cam.following ? "true" : "false");
    }

    void log_finalize(float support_balance, float lean_x, float lean_y) {
        if (!active || !file) return;
        float sway_displacement = support_balance * 0.04f;
        fprintf(file,
            ",\"finalize\":{"
            "\"sway_displacement\":%.4f,"
            "\"support_balance\":%.4f,"
            "\"lean_x\":%.4f,\"lean_y\":%.4f,"
            "\"lean_magnitude\":%.4f}",
            sway_displacement, support_balance,
            lean_x, lean_y,
            sqrtf(lean_x * lean_x + lean_y * lean_y));
    }

    void end_frame() {
        if (!active || !file) return;
        fprintf(file, "}\n");
        fflush(file);
        frame_count++;
    }

    void log_dynamics(const RigState &anim, const Velocity &vel, float dt) {
        if (!active || !file) return;
        glm::vec3 cur_vel(vel.x, vel.y, 0.0f);
        glm::vec3 accel = (dt > 1e-6f) ? ((cur_vel - prev_velocity_log) / dt)
                                       : glm::vec3(0.0f);
        float jerk = (dt > 1e-6f) ? glm::length(accel - prev_accel_log) / dt : 0.0f;
        prev_accel_log    = accel;
        prev_velocity_log = cur_vel;

        float com_offset = glm::length(glm::vec3(anim.smooth_velocity.x,
                                                   anim.smooth_velocity.y, 0.0f)
                                       - cur_vel);
        fprintf(file,
            ",\"dynamics\":{"
            "\"jerk\":%.4f,\"com_offset\":%.4f,"
            "\"lean_x\":%.4f,\"lean_y\":%.4f,"
            "\"smooth_vel\":[%.4f,%.4f]}",
            jerk, com_offset,
            anim.lean_x, anim.lean_y,
            anim.smooth_velocity.x, anim.smooth_velocity.y);
    }