for (int i = 0; i < segments; ++i) {
        int j = (i + 1) % segments;
        // Longitudinal struts (along bone axis)
        emit_strut(verts, ring_A[i], ring_B[i], STRUT_THICKNESS);
        // Circumferential hoops at start ring
        emit_strut(verts, ring_A[i], ring_A[j], STRUT_THICKNESS);
        // Circumferential hoops at end ring
        emit_strut(verts, ring_B[i], ring_B[j], STRUT_THICKNESS);
    }
}

// Unity-style critically-damped spring smoother.
// Smoothly moves 'current' toward 'target' over time.
// 'velocity' is internal state that must persist across calls.
// smooth_time: ~time to reach target (seconds)
static float smooth_damp(float current, float target, float *velocity,
                          float smooth_time, float dt) {
    float omega = 2.0f / smooth_time;
    float x     = omega * dt;
    float exp_f = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float delta = current - target;
    float temp  = (*velocity + omega * delta) * dt;
    *velocity   = (*velocity - omega * temp) * exp_f;
    return target + (delta + temp) * exp_f;
}

// Angle-aware smooth_damp that handles wraparound at ±π.
static float smooth_damp_angle(float current, float target, float *velocity,
                                float smooth_time, float dt) {
    float delta = target - current;
    while (delta >  glm::pi<float>()) delta -= glm::two_pi<float>();
    while (delta < -glm::pi<float>()) delta += glm::two_pi<float>();
    return smooth_damp(current, current + delta, velocity, smooth_time, dt);
}

// Generic two-bone analytical IK solver (extracted from leg IK).
// H: root joint (hip/shoulder), target: end-effector goal,
// a: upper bone length, b: lower bone length,
// pole: pole target for bend direction,
// fallback_perp: fallback perpendicular when pole is degenerate.
static void solve_two_bone(glm::vec3 H, glm::vec3 target,
                           float a, float b,
                           glm::vec3 pole, glm::vec3 fallback_perp,
                           glm::vec3 &out_mid, glm::vec3 &out_end) {
    out_end = target;

    glm::vec3 axis = target - H;
    float D = glm::length(axis);
    float min_D = fabsf(a - b) + 0.001f;
    float max_D = a + b - 0.001f;

    float stretch_limit = max_D * 1.15f;
    if (D > stretch_limit && D > 1e-5f)
        out_end = H + (axis / D) * stretch_limit;

    D = std::max(min_D, std::min(D, max_D));

    if (glm::length(axis) > 1e-5f)
        axis = glm::normalize(axis) * D;
    else
        axis = glm::vec3(0, 0, -D);

    float cos_alpha = (a * a + D * D - b * b) / (2.0f * a * D);
    cos_alpha = std::max(-1.0f, std::min(1.0f, cos_alpha));
    float alpha = acosf(cos_alpha);

    glm::vec3 axis_n = glm::normalize(axis);
    glm::vec3 pole_off = pole - H;
    glm::vec3 perp = pole_off - glm::dot(pole_off, axis_n) * axis_n;
    if (glm::length(perp) > 1e-5f)
        perp = glm::normalize(perp);
    else
        perp = fallback_perp;

    glm::vec3 dir_to_mid = axis_n * cosf(alpha) + perp * sinf(alpha);
    out_mid = H + dir_to_mid * a;
}

void register_rig_systems(flecs::world &ecs,
                           InputSystem    &input,
                           CameraState    &camera,
                           AnimationLogger &anim_log,
                           flecs::entity   player_entity) {

    // =========================================================================
    // 1. PlayerMovementSystem
    //    Input → SmoothDamp velocity → position update
    // =========================================================================
    ecs.system("PlayerMovementSystem")
        .kind(flecs::PostUpdate)
        .run([&input, &ecs, player_entity](flecs::iter &) {
            auto *phase = ecs.get<GamePhase>();
            if (!phase || phase->current != GamePhase::Playing) return;
            if (!player_entity.is_alive()) return;

            auto *t    = player_entity.get_mut<Transform>();
            auto *vel  = player_entity.get_mut<Velocity>();
            auto *gait = player_entity.get_mut<ProceduralGait>();
            auto *anim = player_entity.get_mut<RigState>();
            if (!t || !vel || !gait || !anim) return;

            auto &in = input.state();
            float dt = ecs.delta_time();

            // Desired velocity from raw input.
            float raw_x = 0.0f, raw_y = 0.0f;
            if (in.held[(int)Action::MoveUp])    raw_y -= 1.0f;
            if (in.held[(int)Action::MoveDown])  raw_y += 1.0f;
            if (in.held[(int)Action::MoveLeft])  raw_x -= 1.0f;
            if (in.held[(int)Action::MoveRight]) raw_x += 1.0f;