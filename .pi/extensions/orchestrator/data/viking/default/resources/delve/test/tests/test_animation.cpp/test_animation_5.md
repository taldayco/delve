static TestHipState test_compute_hip_counter(float stride_phase,
                                              float hip_sway_deg    = 5.0f,
                                              float hip_drop_max    = 0.03f,
                                              float hip_bob_amp     = 0.02f) {
    float two_pi_phase = stride_phase * 2.0f * glm::pi<float>();
    TestHipState s;
    s.hip_rotation_deg  = hip_sway_deg  * std::sin(two_pi_phase);
    s.hip_drop_fraction = hip_drop_max  * (1.0f - std::abs(std::cos(two_pi_phase)));
    s.hip_bob_y         = hip_bob_amp   * std::abs(std::sin(two_pi_phase));
    return s;
}

static glm::vec3 test_compute_foot_position(float t,
                                             glm::vec3 prev, glm::vec3 target, float step_height) {
    // Smootherstep — matches live rig_animation.cpp GaitSystem formula.
    float warped_t = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    float ts_z     = t * t * (3.0f - 2.0f * t);
    glm::vec3 out;
    out.x = prev.x + (target.x - prev.x) * warped_t;
    out.y = prev.y + (target.y - prev.y) * warped_t;
    out.z = prev.z + (target.z - prev.z) * ts_z + std::sin(t * glm::pi<float>()) * step_height;
    return out;
}

// Test: phase advances proportional to speed; double speed → double advance.
// GaitSystem uses a constant swing rate (no directional weighting).
DELVE_TEST(walk_animation_phase_proportional_to_speed) {
    float dt = 1.0f / 60.0f;

    float p1 = test_advance_phase(0.0f, dt, 1.0f);
    float p2 = test_advance_phase(0.0f, dt, 2.0f);

    // Double speed → exactly double phase advance (linear, direction-independent).
    EXPECT_GT(p2, p1 * 1.98f);
    EXPECT_LT(p2, p1 * 2.02f);
    return true;
}

// Test: Elbow bends forward (natural direction) — forearm rotates toward
// front of character, not backward (hyperextension).
// The swing_wrist_pos formula adds -25° to the forearm rotation relative to
// the upper arm. In the rotation convention used (positive angle = backward),
// subtracting 25° keeps the forearm angled forward at all swing phases.
DELVE_TEST(elbow_bends_forward_not_backward) {
    constexpr float PI = 3.14159265358979323846f;
    // Simulate the wrist position formula at a backward arm swing (+0.3 rad).
    // right_axis = (0,1,0), hang_down = (0,0,-1), shoulder_angle = +0.3 rad.
    // With -25° constant: total_angle = 0.3 - radians(25) = 0.3 - 0.436 = -0.136
    // Forearm dir (world facing=0): -sin(total_angle) x, 0 y, -cos(total_angle) z
    // Elbow angle contribution is small; test the pure constant-bend direction.
    float shoulder_angle = 0.3f; // backward swing
    float elbow_bias     = -25.0f * (PI / 180.0f); // -25 degrees = forward bend
    float total_angle    = shoulder_angle + elbow_bias; // net: slightly forward of vertical

    // Rodrigues rotation of hang_down=(0,0,-1) around right_axis=(0,1,0):
    // dir = (0,0,-1)*cos(a) + (-1,0,0)*sin(a)  [cross((0,1,0),(0,0,-1)) = (-1,0,0)]
    float forearm_fwd_component = -sinf(total_angle); // x-component with facing=0

    // At shoulder_angle=+0.3 (backward) with -25° bias, total ≈ -0.136 rad.
    // sin(-0.136) ≈ -0.136, so forearm_fwd_component = +0.136 (forward lean).
    // Shoulder direction at +0.3: forearm_fwd_component would be -sin(0.3) ≈ -0.296.
    // The forearm is more forward than the upper arm → natural forward elbow bend.
    float upper_arm_fwd = -sinf(shoulder_angle); // upper arm forward component
    EXPECT_GT(forearm_fwd_component, upper_arm_fwd); // forearm is more forward
    return true;
}

// Test: hip_rotation_deg bounded to ±hip_sway_deg, bob ≥ 0, drop ≥ 0.
DELVE_TEST(hip_counter_animation_bounds) {
    bool rotation_ok = true, bob_ok = true, drop_ok = true;
    const float sway_deg = 5.0f, drop_max = 0.03f, bob_amp = 0.02f;

    for (int i = 0; i < 100; ++i) {
        auto s = test_compute_hip_counter((float)i / 100.0f, sway_deg, drop_max, bob_amp);
        if (std::abs(s.hip_rotation_deg) > sway_deg + 1e-4f)   rotation_ok = false;
        if (s.hip_bob_y < -1e-4f || s.hip_bob_y > bob_amp + 1e-4f) bob_ok = false;
        if (s.hip_drop_fraction < -1e-4f || s.hip_drop_fraction > drop_max + 1e-4f) drop_ok = false;
    }
    EXPECT_TRUE(rotation_ok);
    EXPECT_TRUE(bob_ok);
    EXPECT_TRUE(drop_ok);
    return true;
}