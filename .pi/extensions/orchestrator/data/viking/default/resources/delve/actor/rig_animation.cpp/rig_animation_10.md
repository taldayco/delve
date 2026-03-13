glm::vec3 head_pos = pose.joints[(int)J::HEAD];
                glm::vec3 to_target = look.position - head_pos;
                float horiz_dist = sqrtf(to_target.x * to_target.x + to_target.y * to_target.y);

                // Decompose into yaw/pitch relative to facing
                float target_yaw = 0.0f, target_pitch = 0.0f;
                if (horiz_dist > 0.01f) {
                    float abs_yaw = atan2f(to_target.y, to_target.x);
                    target_yaw = abs_yaw - t.facing;
                    // Wrap to [-PI, PI]
                    while (target_yaw >  glm::pi<float>()) target_yaw -= glm::two_pi<float>();
                    while (target_yaw < -glm::pi<float>()) target_yaw += glm::two_pi<float>();
                    target_pitch = atan2f(to_target.z, horiz_dist);
                }

                // Clamp
                float max_yaw   = glm::radians(70.0f);
                float max_pitch = glm::radians(30.0f);
                target_yaw   = std::clamp(target_yaw,   -max_yaw,   max_yaw);
                target_pitch = std::clamp(target_pitch, -max_pitch, max_pitch);

                // Apply weight
                target_yaw   *= look.weight;
                target_pitch *= look.weight;

                // SmoothDamp toward target
                anim.look_yaw = smooth_damp(anim.look_yaw, target_yaw,
                                             &anim.look_yaw_rate, 0.08f, dt);
                anim.look_pitch = smooth_damp(anim.look_pitch, target_pitch,
                                               &anim.look_pitch_rate, 0.08f, dt);

                // Distribute rotation: HEAD 60%, NECK 30%, CHEST 10%
                float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                // Yaw offset as lateral displacement (simplified)
                auto apply_yaw = [&](Joint j, float frac) {
                    float offset = sinf(anim.look_yaw * frac) * 0.1f;
                    pose.joints[(int)j] += glm::vec3(rght_x * offset, rght_y * offset, 0.0f);
                };
                // Pitch offset as vertical displacement
                auto apply_pitch = [&](Joint j, float frac) {
                    pose.joints[(int)j].z += sinf(anim.look_pitch * frac) * 0.05f;
                };

                apply_yaw(J::HEAD, 0.6f);   apply_pitch(J::HEAD, 0.6f);
                apply_yaw(J::NECK, 0.3f);   apply_pitch(J::NECK, 0.3f);
                apply_yaw(J::CHEST, 0.1f);  apply_pitch(J::CHEST, 0.1f);
            });
        });

    // =========================================================================
    // 7. SkeletonFinaliseSystem
    //    Hip sway, acceleration-driven torso lean, idle micro-motion.
    //    Also computes derived joints: ROOT, SPINE_02, HEAD_END, clavicles,
    //    toes, pole targets, and IK goals.
    // =========================================================================
    ecs.system("SkeletonFinaliseSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform      &t,
                         const Velocity       &vel,
                         const ActorConfig    &cfg,
                         const ProceduralGait &gait,
                         const LegState       *legs,
                         RigState             &anim,
                         RigPose              &pose) {

                using J = Joint;

                float speed   = sqrtf(vel.x * vel.x + vel.y * vel.y);
                float rght_x  = -sinf(anim.visual_facing), rght_y = cosf(anim.visual_facing);

                // ---- Phase 1C: Slope-driven hip tilt ----
                if (legs) {
                    float foot_diff = legs->foot[0].z - legs->foot[1].z;
                    float max_tilt = glm::radians(8.0f);
                    float hip_tilt_target = std::clamp(
                        atan2f(foot_diff, cfg.hip_width * 2.0f),
                        -max_tilt, max_tilt);
                    anim.hip_tilt = smooth_damp(anim.hip_tilt, hip_tilt_target,
                                                &anim.hip_tilt_rate, 0.06f, dt);

                    // Apply as lateral Z offset to hip sockets
                    float tilt_offset = sinf(anim.hip_tilt) * cfg.hip_width;
                    pose.joints[(int)J::L_UPPER_LEG].z -= tilt_offset;
                    pose.joints[(int)J::R_UPPER_LEG].z += tilt_offset;
                    pose.joints[(int)J::HIPS].z += sinf(anim.hip_tilt) * 0.5f * cfg.hip_width
                                                   * (foot_diff > 0 ? 1.0f : -1.0f) * 0.1f;
                }