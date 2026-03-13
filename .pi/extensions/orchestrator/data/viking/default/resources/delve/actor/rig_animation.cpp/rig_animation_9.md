// ---- Leg IK — using shared two-bone solver ----
                // Per-leg pole targets and fallback perpendiculars use a guaranteed
                // orthogonal basis. The outward flare is symmetrical: left knee
                // bends slightly outward-left, right knee outward-right.

                // Left leg: fallback pushes knee outward (−right = left)
                glm::vec3 l_fallback(-rght_x, -rght_y, 0.0f);
                glm::vec3 l_pole = l_upper_leg + glm::vec3(fwd_x * 0.5f - rght_x * 0.08f,
                                                             fwd_y * 0.5f - rght_y * 0.08f, 0.2f);
                glm::vec3 l_lower_leg, l_foot;
                solve_two_bone(l_upper_leg, legs.foot[0], cfg.leg_len, cfg.shin_len,
                               l_pole, l_fallback, l_lower_leg, l_foot);
                pose.joints[(int)J::L_LOWER_LEG] = l_lower_leg;
                pose.joints[(int)J::L_FOOT]      = l_foot;

                // Right leg: fallback pushes knee outward (+right)
                glm::vec3 r_fallback(rght_x, rght_y, 0.0f);
                glm::vec3 r_pole = r_upper_leg + glm::vec3(fwd_x * 0.5f + rght_x * 0.08f,
                                                             fwd_y * 0.5f + rght_y * 0.08f, 0.2f);
                glm::vec3 r_lower_leg, r_foot;
                solve_two_bone(r_upper_leg, legs.foot[1], cfg.leg_len, cfg.shin_len,
                               r_pole, r_fallback, r_lower_leg, r_foot);
                pose.joints[(int)J::R_LOWER_LEG] = r_lower_leg;
                pose.joints[(int)J::R_FOOT]      = r_foot;
            });
        });

    // =========================================================================
    // 5. OverlaySystem (Phase 4)
    //    Additive animation overlays: limp, fatigue, heavy carry.
    // =========================================================================
    ecs.system("OverlaySystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform      &t,
                         const Velocity       &vel,
                         const ActorConfig    &cfg,
                         const ProceduralGait &gait,
                         AnimationOverlay     &overlay,
                         RigPose              &pose) {

                using J = Joint;
                using OT = AnimationOverlay::Type;

                if (overlay.active == OT::None || overlay.intensity < 0.001f) return;

                overlay.phase += dt * 2.0f; // overlay tick

                float I = overlay.intensity;
                float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                switch (overlay.active) {
                case OT::Limp: {
                    // Asymmetric hip drop on left side
                    float limp_drop = sinf(gait.phase) * 0.04f * I;
                    pose.joints[(int)J::L_UPPER_LEG].z -= limp_drop;
                    pose.joints[(int)J::HIPS].z -= fabsf(limp_drop) * 0.3f;
                    break;
                }
                case OT::Fatigue: {
                    // Increased sway + forward lean + slow breathing
                    float fatigue_sway = sinf(overlay.phase * 0.8f) * 0.025f * I;
                    glm::vec3 sway_vec(rght_x * fatigue_sway, rght_y * fatigue_sway, 0.0f);
                    pose.joints[(int)J::CHEST]    += sway_vec;
                    pose.joints[(int)J::NECK]     += sway_vec * 1.2f;
                    pose.joints[(int)J::HEAD]     += sway_vec * 1.4f;
                    // Forward lean
                    float lean = 0.03f * I;
                    pose.joints[(int)J::CHEST] += glm::vec3(fwd_x * lean, fwd_y * lean, 0.0f);
                    pose.joints[(int)J::NECK]  += glm::vec3(fwd_x * lean * 1.3f, fwd_y * lean * 1.3f, 0.0f);
                    break;
                }
                case OT::HeavyCarry: {
                    // Forward lean + hands pulled down
                    float lean = 0.05f * I;
                    pose.joints[(int)J::CHEST] += glm::vec3(fwd_x * lean, fwd_y * lean, 0.0f);
                    pose.joints[(int)J::NECK]  += glm::vec3(fwd_x * lean * 0.8f, fwd_y * lean * 0.8f, 0.0f);
                    // Pull hands down
                    pose.joints[(int)J::L_HAND].z -= 0.06f * I;
                    pose.joints[(int)J::R_HAND].z -= 0.06f * I;
                    break;
                }
                case OT::None: break;
                }
            });
        });

    // =========================================================================
    // 6. LookAtSystem (Phase 2)
    //    Procedural head/neck/chest tracking toward LookAtTarget.
    // =========================================================================
    ecs.system("LookAtSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform    &t,
                         const LookAtTarget &look,
                         RigState           &anim,
                         RigPose            &pose) {

                using J = Joint;

                if (!look.active && anim.look_yaw == 0.0f && anim.look_pitch == 0.0f)
                    return;