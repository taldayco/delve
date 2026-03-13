anim.neck_lean_x = smooth_damp(anim.neck_lean_x, anim.chest_lean_x * 0.7f,
                                               &anim.neck_lean_x_rate, 0.08f, dt);
                anim.neck_lean_y = smooth_damp(anim.neck_lean_y, anim.chest_lean_y * 0.7f,
                                               &anim.neck_lean_y_rate, 0.08f, dt);

                anim.head_lean_x = smooth_damp(anim.head_lean_x, anim.chest_lean_x * 0.5f,
                                               &anim.head_lean_x_rate, 0.12f, dt);
                anim.head_lean_y = smooth_damp(anim.head_lean_y, anim.chest_lean_y * 0.5f,
                                               &anim.head_lean_y_rate, 0.12f, dt);

                anim.lean_x = anim.chest_lean_x;
                anim.lean_y = anim.chest_lean_y;

                pose.joints[(int)J::CHEST] += glm::vec3(anim.chest_lean_x, anim.chest_lean_y, 0.0f);
                pose.joints[(int)J::NECK]  += glm::vec3(anim.neck_lean_x,  anim.neck_lean_y,  0.0f);
                pose.joints[(int)J::HEAD]  += glm::vec3(anim.head_lean_x,  anim.head_lean_y,  0.0f);

                // ---- Chest counter-rotation: lateral offset from hips-chest angular difference ----
                {
                    float lag_angle = anim.visual_facing - anim.chest_facing;
                    while (lag_angle >  glm::pi<float>()) lag_angle -= glm::two_pi<float>();
                    while (lag_angle < -glm::pi<float>()) lag_angle += glm::two_pi<float>();

                    float lag_offset = sinf(lag_angle) * cfg.torso_len * 0.25f;
                    glm::vec3 lag_vec(rght_x * lag_offset, rght_y * lag_offset, 0.0f);

                    pose.joints[(int)J::CHEST]       += lag_vec;
                    pose.joints[(int)J::NECK]        += lag_vec * 0.7f;
                    pose.joints[(int)J::HEAD]        += lag_vec * 0.5f;
                    pose.joints[(int)J::L_UPPER_ARM] += lag_vec;
                    pose.joints[(int)J::R_UPPER_ARM] += lag_vec;
                }

                // ---- Idle micro-motion ----
                float idle_blend = 1.0f - std::min(1.0f, speed / 0.2f);

                anim.breath_phase += dt * glm::two_pi<float>() * 0.6f;
                float breath_offset = sinf(anim.breath_phase) * 0.008f * idle_blend;
                pose.joints[(int)J::CHEST].z += breath_offset;
                pose.joints[(int)J::NECK].z  += breath_offset * 0.6f;
                pose.joints[(int)J::HEAD].z  += breath_offset * 0.4f;

                anim.idle_sway_phase += dt * glm::two_pi<float>() * 0.15f;
                float idle_sway = sinf(anim.idle_sway_phase) * 0.01f * idle_blend;
                glm::vec3 idle_sway_vec(rght_x * idle_sway, rght_y * idle_sway, 0.0f);
                pose.joints[(int)J::HIPS]     += idle_sway_vec;
                pose.joints[(int)J::SPINE_01] += idle_sway_vec;
                pose.joints[(int)J::NECK]     += idle_sway_vec;
                pose.joints[(int)J::HEAD]     += idle_sway_vec;

                // ---- Idle weight shift ----
                if (idle_blend > 0.1f) {
                    anim.idle_weight_phase += dt * glm::two_pi<float>() * 0.3f;
                    float weight_shift = sinf(anim.idle_weight_phase);

                    float hip_shift = weight_shift * 0.04f * idle_blend;
                    glm::vec3 shift_vec(rght_x * hip_shift, rght_y * hip_shift, 0.0f);
                    pose.joints[(int)J::HIPS]        += shift_vec;
                    pose.joints[(int)J::SPINE_01]    += shift_vec * 0.7f;
                    pose.joints[(int)J::L_UPPER_LEG] += shift_vec;
                    pose.joints[(int)J::R_UPPER_LEG] += shift_vec;

                    float weight_dip = (1.0f - fabsf(weight_shift)) * 0.012f * idle_blend;
                    pose.joints[(int)J::HIPS].z     -= weight_dip;
                    pose.joints[(int)J::SPINE_01].z -= weight_dip * 0.5f;

                    glm::vec3 counter_vec = shift_vec * -0.3f;
                    pose.joints[(int)J::CHEST] += counter_vec;
                    pose.joints[(int)J::NECK]  += counter_vec * 0.5f;
                }

                // ---- Compute derived joints ----
                // ROOT: ground anchor from Transform (already at ground level).
                pose.joints[(int)J::ROOT] = glm::vec3(t.x, t.y, t.z);

                // SPINE_02: mid-point between SPINE_01 and CHEST
                pose.joints[(int)J::SPINE_02] = glm::mix(
                    pose.joints[(int)J::SPINE_01],
                    pose.joints[(int)J::CHEST],
                    0.6f);

                // HEAD_END: skull-top nub
                pose.joints[(int)J::HEAD_END] = pose.joints[(int)J::HEAD]
                    + glm::vec3(0.0f, 0.0f, cfg.head_radius);

                // Clavicles: 25% from CHEST toward each upper arm
                pose.joints[(int)J::L_CLAVICLE] = glm::mix(
                    pose.joints[(int)J::CHEST],
                    pose.joints[(int)J::L_UPPER_ARM],
                    0.25f);
                pose.joints[(int)J::R_CLAVICLE] = glm::mix(
                    pose.joints[(int)J::CHEST],
                    pose.joints[(int)J::R_UPPER_ARM],
                    0.25f);