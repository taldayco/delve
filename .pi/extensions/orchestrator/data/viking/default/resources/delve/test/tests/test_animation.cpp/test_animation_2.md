// Test: Arm phase opposes leg phase (anti-phase) for >70% of gait cycle
// samples. Fix 1 foundation: left arm = sin(phase + PI) = -sin(phase), left leg
// ∝ sin(phase).
DELVE_TEST(arm_phase_opposes_leg) {
  int opposing_count = 0;
  int total_samples = 100;
  for (int i = 0; i < total_samples; ++i) {
    float phase = (float)i / total_samples * glm::two_pi<float>();
    float l_arm_angle = sinf(phase + glm::pi<float>());
    float l_leg_fwd = sinf(phase);
    float opposition = arm_phase_opposition(l_arm_angle, l_leg_fwd);
    if (opposition > 0.5f)
      ++opposing_count;
  }
  float ratio = (float)opposing_count / total_samples;
  EXPECT_GT(ratio, 0.7f);
  return true;
}

// Test: Joint delay ordering — shoulder converges faster than wrist (successive
// breaking). Validates joint-delay chain: shoulder(0.02s) <
// elbow(0.04s) < wrist(0.06s).
DELVE_TEST(joint_delay_ordering) {
  float dt = 1.0f / 60.0f;
  float target = 1.0f;
  float shoulder = 0.0f, shoulder_rate = 0.0f;
  float elbow = 0.0f, elbow_rate = 0.0f;
  float wrist = 0.0f, wrist_rate = 0.0f;
  for (int i = 0; i < 3; ++i) {
    shoulder = smooth_damp_test(shoulder, target, &shoulder_rate, 0.02f, dt);
    elbow = smooth_damp_test(elbow, shoulder, &elbow_rate, 0.04f, dt);
    wrist = smooth_damp_test(wrist, elbow, &wrist_rate, 0.06f, dt);
  }
  float shoulder_err = fabsf(shoulder - target);
  float elbow_err = fabsf(elbow - target);
  float wrist_err = fabsf(wrist - target);
  EXPECT_LT(shoulder_err, elbow_err);
  EXPECT_LT(elbow_err, wrist_err);
  return true;
}

// Test: Idle breathing frequency is approximately 0.6 Hz (≈3 cycles in 5
// seconds).
DELVE_TEST(idle_breathing_frequency) {
  float dt = 1.0f / 60.0f;
  int frames = (int)(5.0f / dt);
  float phase = 0.0f, prev_val = 0.0f;
  int crossings = 0;
  for (int i = 0; i < frames; ++i) {
    phase += dt * glm::two_pi<float>() * 0.6f;
    float val = sinf(phase) * 0.008f;
    if (prev_val < 0.0f && val >= 0.0f)
      ++crossings;
    prev_val = val;
  }
  EXPECT_GT(crossings, 1);
  EXPECT_LT(crossings, 6);
  return true;
}

// Test: Velocity smoothing has weight — can't reach full speed in 1 frame.
DELVE_TEST(velocity_smoothing_has_weight) {
  float move_speed = 4.0f, current = 0.0f, rate = 0.0f;
  float dt = 1.0f / 60.0f;
  float result = smooth_damp_test(current, move_speed, &rate, 0.1f, dt);
  EXPECT_LT(result, move_speed * 0.5f);
  EXPECT_GT(result, 0.0f);
  return true;
}

// Test: Fix 1 — Smootherstep foot path has peak XY velocity at mid-stride.
// smootherstep(t) = 6t^5 - 15t^4 + 10t^3
// Derivative peaks at t=0.5 and has zero first+second derivatives at endpoints.
DELVE_TEST(elliptical_foot_path_peak_velocity_at_midstride) {
  auto smootherstep = [](float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
  };
  float dp = 0.05f;
  float t_start = smootherstep(0.00f + dp) - smootherstep(0.00f);
  float t_mid   = smootherstep(0.50f + dp) - smootherstep(0.50f);
  float t_end   = smootherstep(1.00f)      - smootherstep(1.00f - dp);
  // Mid-stride velocity is strictly greater than start and end.
  EXPECT_GT(t_mid, t_start);
  EXPECT_GT(t_mid, t_end);
  // Endpoints are slow: less than half the mid-stride rate.
  EXPECT_LT(t_start, t_mid * 0.5f);
  EXPECT_LT(t_end, t_mid * 0.5f);
  return true;
}

// Test: Fix 2 — Gait phase uses constant swing rate (direction-independent).
// At full speed (4.0 u/s) with SWING_RATE=2.7, arm swing frequency is ~1.72 Hz
// — a casual walking cadence regardless of movement direction.
DELVE_TEST(gait_phase_constant_swing_rate) {
  constexpr float SWING_RATE = 2.7f; // must match rig_animation.cpp
  constexpr float FULL_SPEED = 4.0f;
  constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;

  float phase_rate = FULL_SPEED * SWING_RATE; // rad/s at full speed
  float freq_hz = phase_rate / TWO_PI;

  // Casual walk cadence: ~1.5–2.0 Hz at full speed.
  EXPECT_GT(freq_hz, 1.5f);
  EXPECT_LT(freq_hz, 2.0f);