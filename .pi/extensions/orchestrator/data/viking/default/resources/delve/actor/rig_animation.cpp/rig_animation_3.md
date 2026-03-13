// Rotate -45 degrees to align WASD with isometric screen axes.
            // Isometric camera looks from NE: world(-1,-1) = screen UP.
            // -45° maps W(screen-up) → world(-x,-y), D(screen-right) → world(+x,-y).
            static constexpr float COS_ISO = 0.70710678118f; // cos(45°)
            static constexpr float SIN_ISO = 0.70710678118f; // sin(45°)
            float desired_x = ( raw_x * COS_ISO + raw_y * SIN_ISO) * gait->move_speed;
            float desired_y = (-raw_x * SIN_ISO + raw_y * COS_ISO) * gait->move_speed;

            // SmoothDamp: critically-damped spring gives feeling of mass.
            // smooth_time = 0.1s → reaches ~90% of target in ~0.2s.
            const float smooth_time = 0.1f;
            anim->smooth_velocity.x = smooth_damp(anim->smooth_velocity.x, desired_x,
                                                    &anim->velocity_rate.x, smooth_time, dt);
            anim->smooth_velocity.y = smooth_damp(anim->smooth_velocity.y, desired_y,
                                                    &anim->velocity_rate.y, smooth_time, dt);

            vel->x = anim->smooth_velocity.x;
            vel->y = anim->smooth_velocity.y;

            t->x += vel->x * dt;
            t->y += vel->y * dt;

            float spd = sqrtf(vel->x * vel->x + vel->y * vel->y);
            if (spd > 0.001f)
                t->facing = atan2f(vel->y, vel->x);

            // Turn detection
            float turn_delta = t->facing - anim->visual_facing;
            while (turn_delta >  glm::pi<float>()) turn_delta -= glm::two_pi<float>();
            while (turn_delta < -glm::pi<float>()) turn_delta += glm::two_pi<float>();

            anim->turn_delta     = turn_delta;
            anim->turn_magnitude = fabsf(turn_delta);
            anim->turn_urgency   = anim->turn_magnitude / glm::pi<float>();

            // Hysteresis
            constexpr float TURN_ENTER_RAD = 1.0472f;  // 60°
            constexpr float TURN_EXIT_RAD  = 0.3491f;  // 20°
            if (!anim->in_large_turn && anim->turn_magnitude > TURN_ENTER_RAD)
                anim->in_large_turn = true;
            else if (anim->in_large_turn && anim->turn_magnitude < TURN_EXIT_RAD)
                anim->in_large_turn = false;

            // Adaptive: small turns 0.15s (responsive), full reversal 0.50s (feet can resequence)
            constexpr float SMOOTH_MIN = 0.15f;
            constexpr float SMOOTH_MAX = 0.50f;
            float turn_t = std::clamp(anim->turn_urgency, 0.0f, 1.0f);
            float adaptive_smooth = SMOOTH_MIN + (SMOOTH_MAX - SMOOTH_MIN) * turn_t * turn_t;

            anim->visual_facing = smooth_damp_angle(anim->visual_facing, t->facing,
                                                     &anim->visual_facing_rate, adaptive_smooth, dt);

            // Chest lags behind hips: slower smooth_time creates torsion during turns
            float chest_smooth = (spd > 0.3f) ? 0.22f : 0.06f;
            anim->chest_facing = smooth_damp_angle(anim->chest_facing, anim->visual_facing,
                                                    &anim->chest_facing_rate, chest_smooth, dt);

            if (spd <= 0.001f) {
                anim->visual_facing_rate = 0.0f;
                anim->turn_delta     = 0.0f;
                anim->turn_magnitude = 0.0f;
                anim->turn_urgency   = 0.0f;
                anim->in_large_turn  = false;
                anim->chest_facing_rate = 0.0f;
            }

            // Phase 2C: set look-at target in movement direction
            auto *look = player_entity.get_mut<LookAtTarget>();
            if (look) {
                if (spd > 0.1f) {
                    look->position = glm::vec3(t->x + vel->x / spd * 5.0f,
                                               t->y + vel->y / spd * 5.0f,
                                               t->z);
                    look->active = true;
                    // Ramp weight up when moving
                    look->weight = std::min(1.0f, look->weight + ecs.delta_time() * 4.0f);
                } else {
                    // Ramp weight down when idle
                    look->weight = std::max(0.0f, look->weight - ecs.delta_time() * 2.0f);
                    if (look->weight < 0.01f) look->active = false;
                }
            }
        });

    // =========================================================================
    // 2. ActorGroundingSystem
    //    Snap actor Z to terrain height using adaptive spring.
    // =========================================================================
    ecs.system("ActorGroundingSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            ecs.each([&](ActorTag, Transform &t, const ActorConfig &,
                         const LegState *) {
                // Snap directly to terrain height — no spring, no foot feedback.
                // ROOT is the immutable ground anchor; feet adapt via IK.
                t.z = sample_world_height(*map_data, t.x, t.y);
            });
        });