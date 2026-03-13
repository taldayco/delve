// Ground marker at Transform (terrain-locked), not ROOT joint.
            glm::vec3 ground_pos(t.x, t.y, t.z);
            emit_flat_circle(ground_pos, cfg.torso_radius * 1.5f, trace_color, 16, verts);
            emit_cylinder(ground_pos, j(J::HIPS), 0.012f, trace_color, 4, verts);

            // Left foot trace — at foot's own ground level
            glm::vec3 lfoot_ground(j(J::L_FOOT).x, j(J::L_FOOT).y, j(J::L_FOOT).z);
            emit_cylinder(j(J::L_FOOT), lfoot_ground, 0.008f, trace_color, 4, verts);
            emit_flat_circle(lfoot_ground, cfg.leg_radius * 1.2f, trace_color, 12, verts);

            // Right foot trace — at foot's own ground level
            glm::vec3 rfoot_ground(j(J::R_FOOT).x, j(J::R_FOOT).y, j(J::R_FOOT).z);
            emit_cylinder(j(J::R_FOOT), rfoot_ground, 0.008f, trace_color, 4, verts);
            emit_flat_circle(rfoot_ground, cfg.leg_radius * 1.2f, trace_color, 12, verts);
        }

        // ---- Legs (emitted after root tether so they draw on top) ----
        emit_bone_oct(j(J::HIPS),        j(J::L_UPPER_LEG), cfg.leg_radius * 1.1f,  body_color, verts);
        emit_bone_oct(j(J::L_UPPER_LEG), j(J::L_LOWER_LEG), cfg.leg_radius,         body_color, verts);
        emit_bone_oct(j(J::L_LOWER_LEG), j(J::L_FOOT),      cfg.leg_radius,         body_color, verts);
        emit_bone_oct(j(J::L_FOOT),      j(J::L_TOE),       cfg.leg_radius * 0.6f,  body_color, verts);

        emit_bone_oct(j(J::HIPS),        j(J::R_UPPER_LEG), cfg.leg_radius * 1.1f,  body_color, verts);
        emit_bone_oct(j(J::R_UPPER_LEG), j(J::R_LOWER_LEG), cfg.leg_radius,         body_color, verts);
        emit_bone_oct(j(J::R_LOWER_LEG), j(J::R_FOOT),      cfg.leg_radius,         body_color, verts);
        emit_bone_oct(j(J::R_FOOT),      j(J::R_TOE),       cfg.leg_radius * 0.6f,  body_color, verts);

        // ---- Pelvis bar ----
        emit_bone_oct(j(J::L_UPPER_LEG), j(J::R_UPPER_LEG), cfg.leg_radius * 1.2f, body_color, verts);

        // ---- Spine / torso chain (2 segments: abdomen + chest) ----
        emit_bone_oct(j(J::HIPS),     j(J::SPINE_01), cfg.torso_radius,        body_color, verts);
        emit_bone_oct(j(J::SPINE_01), j(J::CHEST),    cfg.torso_radius,        body_color, verts);

        // ---- Neck / Head chain ----
        emit_bone_oct(j(J::CHEST),    j(J::NECK),     cfg.head_radius * 0.3f, body_color, verts);
        emit_bone_oct(j(J::NECK),     j(J::HEAD_END), cfg.head_radius * 0.55f, body_color, verts);

        // ---- Left arm chain (with clavicle) ----
        emit_bone_oct(j(J::CHEST),       j(J::L_CLAVICLE),  cfg.arm_radius * 1.4f,  body_color, verts);
        emit_bone_oct(j(J::L_CLAVICLE),  j(J::L_UPPER_ARM), cfg.arm_radius * 1.4f,  body_color, verts);
        emit_bone_oct(j(J::L_UPPER_ARM), j(J::L_LOWER_ARM), cfg.arm_radius,         body_color, verts);
        emit_bone_oct(j(J::L_LOWER_ARM), j(J::L_HAND),      cfg.arm_radius * 0.75f, body_color, verts);

        // ---- Right arm chain (with clavicle) ----
        emit_bone_oct(j(J::CHEST),       j(J::R_CLAVICLE),  cfg.arm_radius * 1.4f,  body_color, verts);
        emit_bone_oct(j(J::R_CLAVICLE),  j(J::R_UPPER_ARM), cfg.arm_radius * 1.4f,  body_color, verts);
        emit_bone_oct(j(J::R_UPPER_ARM), j(J::R_LOWER_ARM), cfg.arm_radius,         body_color, verts);
        emit_bone_oct(j(J::R_LOWER_ARM), j(J::R_HAND),      cfg.arm_radius * 0.75f, body_color, verts);

        // ---- Shoulder bar ----
        emit_bone_oct(j(J::L_UPPER_ARM), j(J::R_UPPER_ARM), cfg.arm_radius * 1.0f, body_color, verts);

        // ---- Joint pivot spheres + RGB axis tripods (structural joints 0–23) ----
        {
            glm::vec3 joint_color(0.88f, 0.88f, 0.88f);
            constexpr float SPHERE_R  = 0.035f;
            constexpr float TRIPOD_SZ = 0.085f;
            for (int ji = 0; ji < (int)J::POLE_KNEE_L; ++ji) {
                emit_sphere(pose.joints[ji], SPHERE_R, joint_color, 6, 4, verts);
                emit_tripod(pose.joints[ji], TRIPOD_SZ, verts);
            }
        }