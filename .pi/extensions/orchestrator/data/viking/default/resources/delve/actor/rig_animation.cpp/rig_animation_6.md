if (legs.progress[leg] >= 1.0f) {
                            // Log contact velocity for grounding quality metrics.
                            float step_dist = glm::length(legs.target[leg] - legs.prev_foot[leg]);
                            anim.foot_contact_velocity[leg] = step_dist / adaptive_duration;

                            legs.stepping[leg] = false;
                            legs.foot[leg]     = legs.target[leg];
                            legs.plant_pos[leg] = legs.target[leg];
                            legs.planted[leg]   = true;
                        }
                    }
                }

                // ---- Planted-foot continuous Z tracking (Phase 1A) ----
                for (int leg = 0; leg < 2; ++leg) {
                    if (!legs.stepping[leg]) {
                        float ground_z = sphere_trace_height(*map_data,
                                            legs.foot[leg].x, legs.foot[leg].y, cfg.leg_radius);
                        legs.foot[leg].z = ground_z;
                    }
                }

                // ---- Half-space constraint: force corrective step if foot crossed center line ----
                for (int leg = 0; leg < 2; ++leg) {
                    if (legs.stepping[leg] || legs.stepping[1 - leg]) continue;
                    float lat = (legs.foot[leg].x - t.x) * step_rght_x
                              + (legs.foot[leg].y - t.y) * step_rght_y;
                    // Symmetrical threshold: left (hip_sign=-1) must be on left (lat<0),
                    // right (hip_sign=+1) must be on right (lat>0).
                    bool crossed = (hip_sign[leg] < 0) ? (lat > 0.02f) : (lat < -0.02f);
                    if (crossed) {
                        legs.stepping[leg] = true;
                        legs.progress[leg] = 0.0f;
                        legs.prev_foot[leg] = legs.foot[leg];
                        float c_hip_x = t.x + step_rght_x * hip_sign[leg] * cfg.hip_width;
                        float c_hip_y = t.y + step_rght_y * hip_sign[leg] * cfg.hip_width;
                        float step_travel = speed * adaptive_duration;
                        float target_off  = (gait.stride_len * 0.5f + step_travel * 0.75f) * speed_factor;
                        float tgt_x = c_hip_x + step_dx * target_off;
                        float tgt_y = c_hip_y + step_dy * target_off;
                        legs.target[leg] = {tgt_x, tgt_y,
                            sphere_trace_height(*map_data, tgt_x, tgt_y, cfg.leg_radius)};
                    }
                }

                // ---- Planted-foot world-lock & reach clamp ----
                float max_horiz = gait.stride_len * 0.9f;
                for (int leg = 0; leg < 2; ++leg) {
                    float hip_x = t.x + step_rght_x * hip_sign[leg] * cfg.hip_width;
                    float hip_y = t.y + step_rght_y * hip_sign[leg] * cfg.hip_width;

                    if (!legs.stepping[leg]) {
                        // World-lock: restore XY to plant position
                        if (legs.planted[leg]) {
                            legs.foot[leg].x = legs.plant_pos[leg].x;
                            legs.foot[leg].y = legs.plant_pos[leg].y;
                        }
                        // Reach check: trigger corrective step instead of sliding
                        float dx = legs.foot[leg].x - hip_x;
                        float dy = legs.foot[leg].y - hip_y;
                        float hd = sqrtf(dx * dx + dy * dy);
                        if (hd > max_horiz) {
                            if (!legs.stepping[1 - leg]) {
                                legs.stepping[leg]  = true;
                                legs.progress[leg]  = 0.0f;
                                legs.prev_foot[leg] = legs.foot[leg];
                                float step_travel = speed * adaptive_duration;
                                float target_off  = (gait.stride_len * 0.5f + step_travel * 0.75f) * speed_factor;
                                float tgt_x = hip_x + step_dx * target_off;
                                float tgt_y = hip_y + step_dy * target_off;
                                legs.target[leg] = {tgt_x, tgt_y,
                                    sphere_trace_height(*map_data, tgt_x, tgt_y, cfg.leg_radius)};
                            } else {
                                // Last resort: slide to prevent hyperextension
                                float s = max_horiz / hd;
                                legs.foot[leg].x = hip_x + dx * s;
                                legs.foot[leg].y = hip_y + dy * s;
                                legs.plant_pos[leg] = legs.foot[leg];
                            }
                        }
                    } else if (legs.progress[leg] < 0.4f) {
                        float tdx = legs.target[leg].x - hip_x;
                        float tdy = legs.target[leg].y - hip_y;
                        float th  = sqrtf(tdx * tdx + tdy * tdy);
                        if (th > max_horiz) {
                            float step_travel = speed * adaptive_duration;
                            float target_off  = (gait.stride_len * 0.5f
                                                + step_travel * 0.75f) * speed_factor;
                            legs.target[leg].x = hip_x + step_dx * target_off;
                            legs.target[leg].y = hip_y + step_dy * target_off;
                            legs.target[leg].z = sphere_trace_height(*map_data,
                                                    legs.target[leg].x,
                                                    legs.target[leg].y, cfg.leg_radius);
                        }
                    }
                }
            });
        });