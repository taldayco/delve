// Successive breaking (joint delay chain).
                anim.l_shoulder_smooth = smooth_damp(anim.l_shoulder_smooth, l_arm_target,
                                                      &anim.l_shoulder_rate, 0.02f, dt);
                anim.l_elbow_smooth    = smooth_damp(anim.l_elbow_smooth, anim.l_shoulder_smooth,
                                                      &anim.l_elbow_rate,    0.04f, dt);
                anim.l_wrist_smooth    = smooth_damp(anim.l_wrist_smooth, anim.l_elbow_smooth,
                                                      &anim.l_wrist_rate,    0.06f, dt);

                anim.r_shoulder_smooth = smooth_damp(anim.r_shoulder_smooth, r_arm_target,
                                                      &anim.r_shoulder_rate, 0.02f, dt);
                anim.r_elbow_smooth    = smooth_damp(anim.r_elbow_smooth, anim.r_shoulder_smooth,
                                                      &anim.r_elbow_rate,    0.04f, dt);
                anim.r_wrist_smooth    = smooth_damp(anim.r_wrist_smooth, anim.r_elbow_smooth,
                                                      &anim.r_wrist_rate,    0.06f, dt);

                // Apply arm swing: rotate "hang down" vector by swing angle around chest-facing axis.
                glm::vec3 right_axis(chest_rght_x, chest_rght_y, 0.0f);
                glm::vec3 hang_down(0.0f, 0.0f, -1.0f);

                auto swing_elbow_pos = [&](glm::vec3 shoulder, float shoulder_angle,
                                            float elbow_angle, float arm_len) -> glm::vec3 {
                    float ca = cosf(shoulder_angle);
                    float sa = sinf(shoulder_angle);
                    glm::vec3 k = right_axis;
                    glm::vec3 v = hang_down;
                    glm::vec3 shoulder_dir = v * ca + glm::cross(k, v) * sa + k * glm::dot(k, v) * (1.0f - ca);
                    (void)elbow_angle;
                    return shoulder + shoulder_dir * arm_len;
                };

                auto swing_wrist_pos = [&](glm::vec3 elbow_pos, float shoulder_angle,
                                            float elbow_angle_add, float forearm_len) -> glm::vec3 {
                    float total_angle = shoulder_angle - glm::radians(25.0f) + elbow_angle_add * 0.3f;
                    float ca = cosf(total_angle);
                    float sa = sinf(total_angle);
                    glm::vec3 k = right_axis;
                    glm::vec3 v = hang_down;
                    glm::vec3 dir = v * ca + glm::cross(k, v) * sa + k * glm::dot(k, v) * (1.0f - ca);
                    return elbow_pos + dir * forearm_len;
                };

                // Left arm FK.
                glm::vec3 l_lower_arm_fk = swing_elbow_pos(l_upper_arm, anim.l_shoulder_smooth,
                                                            anim.l_elbow_smooth, cfg.arm_len);
                glm::vec3 l_hand_fk      = swing_wrist_pos(l_lower_arm_fk, anim.l_shoulder_smooth,
                                                            anim.l_wrist_smooth, cfg.forearm_len);

                // Right arm FK.
                glm::vec3 r_lower_arm_fk = swing_elbow_pos(r_upper_arm, anim.r_shoulder_smooth,
                                                            anim.r_elbow_smooth, cfg.arm_len);
                glm::vec3 r_hand_fk      = swing_wrist_pos(r_lower_arm_fk, anim.r_shoulder_smooth,
                                                            anim.r_wrist_smooth, cfg.forearm_len);

                // Phase 3C: Blend arm IK with FK when ArmIKGoal is present
                glm::vec3 l_lower_arm = l_lower_arm_fk, l_hand = l_hand_fk;
                glm::vec3 r_lower_arm = r_lower_arm_fk, r_hand = r_hand_fk;

                if (arm_ik) {
                    glm::vec3 fwd3_chest(chest_fwd_x, chest_fwd_y, 0.0f);

                    if (arm_ik->weight_l > 0.001f) {
                        glm::vec3 l_elbow_pole = l_upper_arm - fwd3_chest * 0.3f + glm::vec3(0,0,-0.1f);
                        glm::vec3 l_elbow_ik, l_hand_ik;
                        solve_two_bone(l_upper_arm, arm_ik->target_l,
                                       cfg.arm_len, cfg.forearm_len,
                                       l_elbow_pole, glm::vec3(-chest_rght_x, -chest_rght_y, 0.0f),
                                       l_elbow_ik, l_hand_ik);
                        float w = arm_ik->weight_l;
                        l_lower_arm = glm::mix(l_lower_arm_fk, l_elbow_ik, w);
                        l_hand      = glm::mix(l_hand_fk, l_hand_ik, w);
                    }
                    if (arm_ik->weight_r > 0.001f) {
                        glm::vec3 r_elbow_pole = r_upper_arm - fwd3_chest * 0.3f + glm::vec3(0,0,-0.1f);
                        glm::vec3 r_elbow_ik, r_hand_ik;
                        solve_two_bone(r_upper_arm, arm_ik->target_r,
                                       cfg.arm_len, cfg.forearm_len,
                                       r_elbow_pole, glm::vec3(chest_rght_x, chest_rght_y, 0.0f),
                                       r_elbow_ik, r_hand_ik);
                        float w = arm_ik->weight_r;
                        r_lower_arm = glm::mix(r_lower_arm_fk, r_elbow_ik, w);
                        r_hand      = glm::mix(r_hand_fk, r_hand_ik, w);
                    }
                }

                pose.joints[(int)J::L_LOWER_ARM] = l_lower_arm;
                pose.joints[(int)J::L_HAND]      = l_hand;
                pose.joints[(int)J::R_LOWER_ARM] = r_lower_arm;
                pose.joints[(int)J::R_HAND]      = r_hand;