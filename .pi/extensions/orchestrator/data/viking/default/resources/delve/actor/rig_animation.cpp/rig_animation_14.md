// --- C. Limbs (bend-plane-normal method) ---
                auto compute_limb_chain = [&](Joint upper, Joint lower, Joint end,
                                              Joint toe, const glm::vec3 &fallback_perp,
                                              bool is_leg) {
                    glm::vec3 upper_dir = jt[(int)lower] - jt[(int)upper];
                    glm::vec3 lower_dir = jt[(int)end]   - jt[(int)lower];
                    float upper_len = glm::length(upper_dir);
                    float lower_len = glm::length(lower_dir);

                    // Bend plane normal from cross of bone directions
                    glm::vec3 bend_normal = fallback_perp;
                    if (upper_len > 1e-5f && lower_len > 1e-5f) {
                        glm::vec3 bn = glm::cross(
                            upper_dir / upper_len, lower_dir / lower_len);
                        float bn_len = glm::length(bn);
                        if (bn_len > 1e-5f) bend_normal = bn / bn_len;
                    }

                    // Upper bone
                    if (upper_len > 1e-5f) {
                        glm::vec3 up = upper_dir / upper_len;
                        glm::vec3 right = bend_normal;
                        glm::vec3 fwd = glm::cross(up, right);
                        right = glm::cross(fwd, up); // re-orthogonalize
                        xforms.bones[(int)upper] = make_bone_mat4(right, fwd, up, jt[(int)upper]);
                    } else {
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)upper], 1.0f);
                        xforms.bones[(int)upper] = m;
                    }

                    // Lower bone
                    if (lower_len > 1e-5f) {
                        glm::vec3 up = lower_dir / lower_len;
                        // Project bend_normal perpendicular to lower bone direction
                        glm::vec3 right = bend_normal - glm::dot(bend_normal, up) * up;
                        float rlen = glm::length(right);
                        if (rlen < 1e-5f) {
                            // Fallback: project fallback_perp
                            right = fallback_perp - glm::dot(fallback_perp, up) * up;
                            rlen = glm::length(right);
                            if (rlen < 1e-5f) {
                                // Arbitrary perpendicular
                                glm::vec3 alt = (std::abs(up.z) < 0.9f)
                                    ? glm::vec3(0.0f, 0.0f, 1.0f)
                                    : glm::vec3(1.0f, 0.0f, 0.0f);
                                right = glm::normalize(glm::cross(alt, up));
                                rlen = 1.0f;
                            }
                        }
                        right /= rlen;
                        glm::vec3 fwd = glm::cross(up, right);
                        xforms.bones[(int)lower] = make_bone_mat4(right, fwd, up, jt[(int)lower]);
                    } else {
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)lower], 1.0f);
                        xforms.bones[(int)lower] = m;
                    }

                    // End effector (foot or hand)
                    if (is_leg) {
                        // Foot: forward = toe direction, up = world Z
                        glm::vec3 toe_dir = jt[(int)toe] - jt[(int)end];
                        float toe_len = glm::length(toe_dir);
                        glm::vec3 fwd = (toe_len > 1e-5f)
                            ? toe_dir / toe_len : vis_fwd;
                        glm::vec3 up(0.0f, 0.0f, 1.0f);
                        glm::vec3 right = glm::cross(fwd, up);
                        float rlen = glm::length(right);
                        if (rlen < 1e-5f) right = vis_rght;
                        else right /= rlen;
                        up = glm::cross(right, fwd);
                        xforms.bones[(int)end] = make_bone_mat4(right, fwd, up, jt[(int)end]);
                        // Toe: copy foot rotation at toe position
                        glm::mat4 m = xforms.bones[(int)end];
                        m[3] = glm::vec4(jt[(int)toe], 1.0f);
                        xforms.bones[(int)toe] = m;
                    } else {
                        // Hand: continue forearm direction
                        if (lower_len > 1e-5f) {
                            glm::mat4 m = xforms.bones[(int)lower];
                            m[3] = glm::vec4(jt[(int)end], 1.0f);
                            xforms.bones[(int)end] = m;
                        } else {
                            glm::mat4 m(1.0f);
                            m[3] = glm::vec4(jt[(int)end], 1.0f);
                            xforms.bones[(int)end] = m;
                        }
                    }
                };

                // Left leg
                compute_limb_chain(J::L_UPPER_LEG, J::L_LOWER_LEG, J::L_FOOT, J::L_TOE,
                    glm::vec3(-vis_rght.x, -vis_rght.y, 0.0f), true);
                // Right leg
                compute_limb_chain(J::R_UPPER_LEG, J::R_LOWER_LEG, J::R_FOOT, J::R_TOE,
                    glm::vec3(vis_rght.x, vis_rght.y, 0.0f), true);
                // Left arm
                compute_limb_chain(J::L_UPPER_ARM, J::L_LOWER_ARM, J::L_HAND, J::L_HAND,
                    glm::vec3(-chest_fwd.x, -chest_fwd.y, 0.0f), false);
                // Right arm
                compute_limb_chain(J::R_UPPER_ARM, J::R_LOWER_ARM, J::R_HAND, J::R_HAND,
                    glm::vec3(-chest_fwd.x, -chest_fwd.y, 0.0f), false);