// =========================================================================
    // 3.5. GrabDriveSystem (Phase 5)
    //      GrabState → ArmIKGoal + weight compensation on gait.
    // =========================================================================
    ecs.system("GrabDriveSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([&](ActorTag,
                         const GrabState       &grab,
                         const ActorConfig     &cfg,
                         ArmIKGoal             &arm_ik,
                         ProceduralGait        &gait,
                         AnimationOverlay      *overlay) {

                if (grab.active_l) {
                    arm_ik.target_l = grab.grab_point;
                    arm_ik.weight_l = grab.weight;
                } else {
                    arm_ik.weight_l = 0.0f;
                }
                if (grab.active_r) {
                    arm_ik.target_r = grab.grab_point;
                    arm_ik.weight_r = grab.weight;
                } else {
                    arm_ik.weight_r = 0.0f;
                }

                // Weight compensation: reduce stride when carrying
                float carry_w = std::max(grab.active_l ? grab.weight : 0.0f,
                                         grab.active_r ? grab.weight : 0.0f);
                if (carry_w > 0.01f) {
                    // Activate heavy carry overlay if available
                    if (overlay) {
                        overlay->active = AnimationOverlay::Type::HeavyCarry;
                        overlay->intensity = carry_w;
                    }
                }
            });
        });

    // =========================================================================
    // 4. IKSystem
    //    Two-bone analytical leg IK + pendulum arm swing with joint delay chain.
    // =========================================================================
    ecs.system("IKSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform  &t,
                         const Velocity   &vel,
                         const LegState   &legs,
                         const ActorConfig &cfg,
                         const ProceduralGait &gait,
                         const ArmIKGoal  *arm_ik,
                         RigState         &anim,
                         RigPose          &pose) {

                using J = Joint;

                float facing = anim.visual_facing;
                float fwd_x  =  cosf(facing), fwd_y  = sinf(facing);
                float rght_x = -sinf(facing), rght_y = cosf(facing);

                // Chest-facing vectors (lags behind visual_facing for torsion)
                float chest_fwd_x  =  cosf(anim.chest_facing), chest_fwd_y  = sinf(anim.chest_facing);
                float chest_rght_x = -sinf(anim.chest_facing), chest_rght_y = cosf(anim.chest_facing);

                // HIPS derived upward from ROOT (Transform is ground-level).
                float leg_height = cfg.leg_len + cfg.shin_len;
                glm::vec3 hips(t.x, t.y, t.z + leg_height);
                glm::vec3 spine01 = hips  + glm::vec3(0, 0, cfg.torso_len * 0.4f);
                glm::vec3 chest   = hips  + glm::vec3(0, 0, cfg.torso_len);
                glm::vec3 neck    = chest + glm::vec3(0, 0, cfg.neck_len);
                glm::vec3 head    = neck  + glm::vec3(0, 0, cfg.head_radius);

                pose.joints[(int)J::HIPS]     = hips;
                pose.joints[(int)J::SPINE_01] = spine01;
                pose.joints[(int)J::CHEST]    = chest;
                pose.joints[(int)J::NECK]     = neck;
                pose.joints[(int)J::HEAD]     = head;

                // Hip sockets (L_UPPER_LEG / R_UPPER_LEG).
                float hip_z = t.z + leg_height;
                glm::vec3 l_upper_leg(t.x - rght_x * cfg.hip_width, t.y - rght_y * cfg.hip_width, hip_z);
                glm::vec3 r_upper_leg(t.x + rght_x * cfg.hip_width, t.y + rght_y * cfg.hip_width, hip_z);
                pose.joints[(int)J::L_UPPER_LEG] = l_upper_leg;
                pose.joints[(int)J::R_UPPER_LEG] = r_upper_leg;

                // Shoulder sockets use chest_facing (L_UPPER_ARM / R_UPPER_ARM).
                glm::vec3 l_upper_arm(chest.x - chest_rght_x * cfg.shoulder_width,
                                      chest.y - chest_rght_y * cfg.shoulder_width,
                                      chest.z);
                glm::vec3 r_upper_arm(chest.x + chest_rght_x * cfg.shoulder_width,
                                      chest.y + chest_rght_y * cfg.shoulder_width,
                                      chest.z);
                pose.joints[(int)J::L_UPPER_ARM] = l_upper_arm;
                pose.joints[(int)J::R_UPPER_ARM] = r_upper_arm;

                // ---- Pendulum arm swing (FK) ----
                float speed       = sqrtf(vel.x * vel.x + vel.y * vel.y);
                float swing_amp   = std::min(1.0f, speed / gait.move_speed) * glm::radians(30.0f);

                float l_arm_target = sinf(gait.phase + glm::pi<float>()) * swing_amp;
                float r_arm_target = sinf(gait.phase)                      * swing_amp;
                anim.l_arm_target = l_arm_target;
                anim.r_arm_target = r_arm_target;