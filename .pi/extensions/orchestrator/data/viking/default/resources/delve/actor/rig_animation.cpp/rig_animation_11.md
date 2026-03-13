// ---- CoM hip sway (leg-driven) ----
                // Target: shift toward planted foot (-1 left, +1 right)
                float balance_target = 0.0f;
                if (legs) {
                    if (legs->stepping[0] && !legs->stepping[1]) balance_target =  1.0f;
                    else if (legs->stepping[1] && !legs->stepping[0]) balance_target = -1.0f;
                }
                anim.support_balance = smooth_damp(anim.support_balance, balance_target,
                                                    &anim.support_balance_rate, 0.08f, dt);
                float sway = anim.support_balance * 0.04f;
                glm::vec3 sway_vec(rght_x * sway, rght_y * sway, 0.0f);
                pose.joints[(int)J::HIPS]     += sway_vec;
                pose.joints[(int)J::SPINE_01] += sway_vec;
                pose.joints[(int)J::CHEST]    += sway_vec;
                pose.joints[(int)J::NECK]     += sway_vec;
                pose.joints[(int)J::HEAD]     += sway_vec;

                // Fix 3: Hip counter-animation (CoM shift + double-bounce).
                float walk_blend = std::min(1.0f, speed / (gait.move_speed * 0.3f));

                // Turn urgency from visual_facing_rate
                float turn_urgency = std::min(1.0f, fabsf(anim.visual_facing_rate) / 10.0f);

                float target_hip_roll = sinf(gait.phase) * 0.06f * walk_blend;
                anim.hip_roll = smooth_damp(anim.hip_roll, target_hip_roll,
                                            &anim.hip_roll_rate, 0.04f, dt);

                // Gait-phase bob: fade during turns (it's steady-walk motion)
                float bob_blend = walk_blend * (1.0f - turn_urgency * 0.7f);
                float target_hip_bob = fabsf(sinf(gait.phase)) * 0.018f * bob_blend;
                anim.hip_bob = smooth_damp(anim.hip_bob, target_hip_bob,
                                           &anim.hip_bob_rate, 0.03f, dt);

                // Step-event hip dip: parabolic when foot is airborne
                float step_dip_target = 0.0f;
                if (legs) {
                    float leg_height_dip = cfg.leg_len + cfg.shin_len;
                    float base_peak = leg_height_dip * 0.07f;
                    float turn_amp = 1.0f + turn_urgency * 0.5f;
                    float peak = base_peak * turn_amp * walk_blend;

                    for (int i = 0; i < 2; ++i) {
                        if (legs->stepping[i]) {
                            float p = legs->progress[i];
                            float dip = peak * 4.0f * p * (1.0f - p);
                            step_dip_target = std::max(step_dip_target, dip);
                        }
                    }
                }
                anim.hip_dip = smooth_damp(anim.hip_dip, step_dip_target,
                                           &anim.hip_dip_rate, 0.025f, dt);

                // Apply hip roll as a lateral CoM shift.
                float roll_shift = sinf(anim.hip_roll) * cfg.hip_width * 0.4f;
                glm::vec3 roll_vec(rght_x * roll_shift, rght_y * roll_shift, 0.0f);
                pose.joints[(int)J::HIPS]        += roll_vec;
                pose.joints[(int)J::SPINE_01]    += roll_vec * 0.6f;
                pose.joints[(int)J::L_UPPER_LEG] += roll_vec;
                pose.joints[(int)J::R_UPPER_LEG] += roll_vec;

                // Combined vertical offset: bob (up) + dip (down)
                float hip_z_offset = anim.hip_bob - anim.hip_dip;
                pose.joints[(int)J::HIPS].z         += hip_z_offset;
                pose.joints[(int)J::SPINE_01].z     += hip_z_offset * 0.7f;
                pose.joints[(int)J::L_UPPER_LEG].z  += hip_z_offset;
                pose.joints[(int)J::R_UPPER_LEG].z  += hip_z_offset;

                // ---- Acceleration-driven torso lean ----
                glm::vec3 cur_vel(vel.x, vel.y, 0.0f);
                glm::vec3 accel = (dt > 1e-6f) ? ((cur_vel - anim.prev_velocity) / dt)
                                               : glm::vec3(0.0f);
                anim.prev_velocity = cur_vel;

                float accel_len = glm::length(accel);
                const float max_lean = glm::radians(8.0f);
                float lean_factor = std::min(accel_len * 0.015f, max_lean);

                glm::vec3 lean_dir{0.0f};
                if (accel_len > 0.01f)
                    lean_dir = glm::normalize(accel);

                float target_lean_x = lean_dir.x * lean_factor;
                float target_lean_y = lean_dir.y * lean_factor;

                float fwd_x = cosf(anim.visual_facing), fwd_y = sinf(anim.visual_facing);
                float speed_blend = std::min(1.0f, speed / gait.move_speed);
                float fwd_lean = speed_blend * glm::radians(2.5f);
                target_lean_x += fwd_x * fwd_lean;
                target_lean_y += fwd_y * fwd_lean;

                anim.chest_lean_x = smooth_damp(anim.chest_lean_x, target_lean_x,
                                                 &anim.chest_lean_x_rate, 0.05f, dt);
                anim.chest_lean_y = smooth_damp(anim.chest_lean_y, target_lean_y,
                                                 &anim.chest_lean_y_rate, 0.05f, dt);