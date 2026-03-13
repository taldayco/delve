// =========================================================================
    // 3. GaitSystem
    //    Procedural foot placement with one-foot-planted invariant.
    //    Speed-adaptive step duration for natural cadence.
    // =========================================================================
    ecs.system("GaitSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform &t,
                         const Velocity   &vel,
                         ProceduralGait   &gait,
                         LegState         &legs,
                         RigState         &anim,
                         const ActorConfig &cfg) {

                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);

                float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
                float vel_dx = speed > 0.001f ? vel.x / speed : fwd_x;
                float vel_dy = speed > 0.001f ? vel.y / speed : fwd_y;

                // Turn urgency (local, for GaitSystem use)
                float facing_delta = t.facing - anim.visual_facing;
                while (facing_delta >  glm::pi<float>()) facing_delta -= glm::two_pi<float>();
                while (facing_delta < -glm::pi<float>()) facing_delta += glm::two_pi<float>();
                float turn_urgency = std::min(1.0f, fabsf(facing_delta) / glm::pi<float>());

                // Visual-facing forward
                float vf_fwd_x = cosf(anim.visual_facing);
                float vf_fwd_y = sinf(anim.visual_facing);

                // Blend vel direction toward visual_facing during turns (max 70% blend)
                float turn_blend = turn_urgency * 0.7f;
                float step_dx = vel_dx * (1.0f - turn_blend) + vf_fwd_x * turn_blend;
                float step_dy = vel_dy * (1.0f - turn_blend) + vf_fwd_y * turn_blend;
                float step_len = sqrtf(step_dx * step_dx + step_dy * step_dy);
                if (step_len > 0.001f) { step_dx /= step_len; step_dy /= step_len; }
                else { step_dx = vf_fwd_x; step_dy = vf_fwd_y; }

                // Gait phase drives arm swing and hip oscillation only (foot
                // placement is distance-based and unaffected by this phase).
                // Rate chosen so full-speed (4 u/s) gives ~1.27 Hz swing — a
                // natural walking cadence.  Direction-independent: the old
                // iso-vertical multiplier made arms/hips speed up depending on
                // screen direction, which looked unnatural.
                // Casual walk cadence: ~1.7 Hz at full speed (4 u/s).
                // 2.7 rad/s per unit speed → ω = 10.8 rad/s → T ≈ 0.58s.
                constexpr float SWING_RATE = 2.7f; // rad/s per unit speed
                gait.phase += speed * dt * SWING_RATE;

                (void)0; // rght_x/rght_y removed — unified basis below

                // Unified right vector derived from step direction (orthogonal to travel).
                // This ensures hip offsets and half-space math use the same basis as
                // foot placement, preventing asymmetric reach during turns.
                float step_rght_x = -step_dy, step_rght_y = step_dx;

                // Hip socket XY offsets (legs offset left/right of facing).
                float hip_sign[2] = { -1.0f, 1.0f }; // left, right

                // Speed-adaptive step duration: faster movement = quicker steps.
                float speed_ratio      = std::max(0.4f, std::min(1.0f, speed / gait.move_speed));
                float adaptive_duration = gait.step_duration / speed_ratio;
                adaptive_duration *= (1.0f - turn_urgency * 0.3f);  // 30% faster steps during turns

                // Blend forward prediction toward zero when idle so feet
                // return to a neutral stance directly under the hips.
                float speed_factor = std::min(1.0f, speed / (gait.move_speed * 0.15f));
                speed_factor *= (1.0f - turn_urgency * 0.5f);  // reduce forward prediction during pivots