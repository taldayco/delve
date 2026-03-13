// After one step, the value should have decreased (going the short way through -180°)
    float after_one = smooth_damp_angle_test(current, target, &velocity, 0.15f, dt);
    // The shortest path from -170° to +170° is through ±180° (20° gap)
    // So the value should move toward -180° (decreasing), not toward 0° (increasing)
    EXPECT_LT(after_one, current);
    return true;
}

DELVE_TEST(visual_facing_lags_behind_facing) {
    // After 1 frame at a new facing, visual_facing should have moved but not converged
    float visual_facing = 0.0f;
    float visual_facing_rate = 0.0f;
    float target_facing = glm::half_pi<float>();
    float dt = 1.0f / 60.0f;

    visual_facing = smooth_damp_angle_test(visual_facing, target_facing,
                                            &visual_facing_rate, 0.15f, dt);

    // Should have moved toward target but not reached it
    EXPECT_GT(visual_facing, 0.01f);
    EXPECT_LT(visual_facing, target_facing * 0.5f);

    // After 1 second (60 more frames), should be very close
    for (int i = 0; i < 60; ++i)
        visual_facing = smooth_damp_angle_test(visual_facing, target_facing,
                                                &visual_facing_rate, 0.15f, dt);
    EXPECT_NEAR(visual_facing, target_facing, 0.02f);
    return true;
}

DELVE_TEST(no_leg_crossing_during_abrupt_turn) {
    // Simulate a 180° turn and verify no foot crosses the character's center line
    LegState legs{};
    ProceduralGait gait{};
    ActorConfig cfg;
    float dt = 1.0f / 60.0f;

    float pos_x = 0.0f, pos_y = 0.0f;
    float vel_x = 4.0f, vel_y = 0.0f;
    float visual_facing = 0.0f;
    float visual_facing_rate = 0.0f;

    // Start feet at neutral under hips
    legs.foot[0] = {-cfg.hip_width, 0.0f, 0.0f};
    legs.foot[1] = { cfg.hip_width, 0.0f, 0.0f};

    bool any_crossed = false;
    float hip_sign[2] = {-1.0f, 1.0f};

    // 30 frames right, then 120 frames left
    for (int frame = 0; frame < 150; ++frame) {
        if (frame == 30) { vel_x = -4.0f; }
        pos_x += vel_x * dt;
        float speed = fabsf(vel_x);
        float facing = (vel_x > 0) ? 0.0f : glm::pi<float>();

        // Update visual_facing
        {
            float delta = facing - visual_facing;
            while (delta >  glm::pi<float>()) delta -= glm::two_pi<float>();
            while (delta < -glm::pi<float>()) delta += glm::two_pi<float>();
            visual_facing = smooth_damp_test(visual_facing, visual_facing + delta,
                                              &visual_facing_rate, 0.15f, dt);
        }

        float vf_rght_x = -sinf(visual_facing), vf_rght_y = cosf(visual_facing);
        float vel_dx = vel_x / speed;
        float speed_factor = std::min(1.0f, speed / (gait.move_speed * 0.15f));

        float speed_ratio = std::max(0.4f, std::min(1.0f, speed / gait.move_speed));
        float adaptive_duration = gait.step_duration / speed_ratio;

        // Step initiation (simplified, 1D)
        for (int leg = 0; leg < 2; ++leg) {
            float hip_x = pos_x + vf_rght_x * hip_sign[leg] * cfg.hip_width;
            float half_stride = gait.stride_len * 0.5f;
            float center_x = hip_x + vel_dx * half_stride * speed_factor;

            if (!legs.stepping[leg]) {
                float dx = legs.foot[leg].x - center_x;
                float trigger = half_stride * (0.25f + 0.75f * speed_factor);
                if (fabsf(dx) > trigger && !legs.stepping[1 - leg]) {
                    legs.stepping[leg] = true;
                    legs.progress[leg] = 0.0f;
                    legs.prev_foot[leg] = legs.foot[leg];
                    float step_travel = speed * adaptive_duration;
                    float target_off = (half_stride + step_travel * 0.75f) * speed_factor;
                    float tgt_x = hip_x + vel_dx * target_off;
                    // Half-space clamp
                    float tgt_lat = (tgt_x - pos_x) * vf_rght_x;
                    if ((hip_sign[leg] < 0 && tgt_lat > 0) || (hip_sign[leg] > 0 && tgt_lat < 0))
                        tgt_x = pos_x + vf_rght_x * hip_sign[leg] * 0.05f;
                    legs.target[leg] = {tgt_x, 0.0f, 0.0f};
                }
            }
            if (legs.stepping[leg]) {
                legs.progress[leg] += dt / adaptive_duration;
                float p = std::min(legs.progress[leg], 1.0f);
                float w = p*p*p*(p*(p*6.0f-15.0f)+10.0f);
                legs.foot[leg].x = legs.prev_foot[leg].x +
                    (legs.target[leg].x - legs.prev_foot[leg].x) * w;
                if (legs.progress[leg] >= 1.0f) {
                    legs.stepping[leg] = false;
                    legs.foot[leg] = legs.target[leg];
                }
            }
        }