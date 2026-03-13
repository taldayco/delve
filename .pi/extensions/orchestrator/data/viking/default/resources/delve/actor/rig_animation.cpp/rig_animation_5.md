// Turn-aware step sequencing: trailing foot steps first.
                // Use the unified step basis (step_dx/dy) for consistent behind calculation.
                if (turn_urgency > 0.4f && !legs.stepping[0] && !legs.stepping[1]) {
                    float behind[2];
                    for (int i = 0; i < 2; ++i) {
                        float fx = legs.foot[i].x - t.x;
                        float fy = legs.foot[i].y - t.y;
                        behind[i] = fx * step_dx + fy * step_dy;
                    }
                    legs.turn_step_queued = (behind[0] < behind[1]) ? 0 : 1;
                }
                // Starvation guard: if turn_urgency drops below threshold while a
                // step is queued but neither leg is stepping, reset the queue so the
                // blocked leg isn't permanently locked out by floating-point drift.
                if (turn_urgency <= 0.4f && legs.turn_step_queued >= 0
                    && !legs.stepping[0] && !legs.stepping[1]) {
                    legs.turn_step_queued = -1;
                }

                for (int leg = 0; leg < 2; ++leg) {
                    int other_leg = 1 - leg;

                    float hip_x = t.x + step_rght_x * hip_sign[leg] * cfg.hip_width;
                    float hip_y = t.y + step_rght_y * hip_sign[leg] * cfg.hip_width;

                    // Trigger check: how far the planted foot is from nominal center.
                    // When idle, center collapses to hip position (no forward offset)
                    // so feet that landed ahead during walking trigger return steps.
                    float half_stride = gait.stride_len * 0.5f;
                    float center_off = half_stride * speed_factor;
                    float center_x = hip_x + step_dx * center_off;
                    float center_y = hip_y + step_dy * center_off;

                    if (!legs.stepping[leg]) {
                        float dx   = legs.foot[leg].x - center_x;
                        float dy   = legs.foot[leg].y - center_y;
                        float dist = sqrtf(dx * dx + dy * dy);

                        // One-foot-planted invariant: only step if other foot is planted.
                        // Lower trigger threshold when idle so return steps fire sooner.
                        bool other_planted = !legs.stepping[other_leg];
                        float trigger_dist = half_stride * (0.25f + 0.75f * speed_factor);
                        bool turn_blocked = (turn_urgency > 0.4f
                                             && legs.turn_step_queued >= 0
                                             && legs.turn_step_queued != leg);
                        if (dist > trigger_dist && other_planted && !turn_blocked) {
                            legs.stepping[leg]  = true;
                            legs.progress[leg]  = 0.0f;
                            legs.prev_foot[leg] = legs.foot[leg];
                            if (legs.turn_step_queued == leg)
                                legs.turn_step_queued = -1;

                            // Velocity-predicted target: foot lands ahead of where
                            // the hip will be when the step completes, implementing
                            // heel-strike placement (inverted pendulum model).
                            // When idle, target collapses to hip position (neutral stance).
                            float step_travel = speed * adaptive_duration;
                            float target_off  = (half_stride + step_travel * 0.75f) * speed_factor;
                            float tgt_x = hip_x + step_dx * target_off;
                            float tgt_y = hip_y + step_dy * target_off;

                            // Half-space clamp: keep target on correct side of center line
                            float tgt_lat = (tgt_x - t.x) * step_rght_x + (tgt_y - t.y) * step_rght_y;
                            if ((hip_sign[leg] < 0 && tgt_lat > 0.02f) || (hip_sign[leg] > 0 && tgt_lat < -0.02f)) {
                                tgt_x -= step_rght_x * (tgt_lat - hip_sign[leg] * 0.05f);
                                tgt_y -= step_rght_y * (tgt_lat - hip_sign[leg] * 0.05f);
                            }

                            float tgt_z = sphere_trace_height(*map_data, tgt_x, tgt_y, cfg.leg_radius);
                            legs.target[leg]    = {tgt_x, tgt_y, tgt_z};
                        }
                    }

                    if (legs.stepping[leg]) {
                        legs.progress[leg] += dt / adaptive_duration;
                        float progress = std::min(legs.progress[leg], 1.0f);

                        // Smootherstep interpolation for XY: steeper S-curve than cosine-ease.
                        float warped_t = progress * progress * progress
                                         * (progress * (progress * 6.0f - 15.0f) + 10.0f);

                        // Z height arc: half-sine lift peaking at mid-stride.
                        float ts_z  = progress * progress * (3.0f - 2.0f * progress);

                        legs.foot[leg].x = legs.prev_foot[leg].x + (legs.target[leg].x - legs.prev_foot[leg].x) * warped_t;
                        legs.foot[leg].y = legs.prev_foot[leg].y + (legs.target[leg].y - legs.prev_foot[leg].y) * warped_t;
                        legs.foot[leg].z = legs.prev_foot[leg].z + (legs.target[leg].z - legs.prev_foot[leg].z) * ts_z
                                           + sinf(progress * glm::pi<float>()) * gait.step_height;