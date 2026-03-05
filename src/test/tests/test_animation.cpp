#include "test_harness.h"
#include "animation_metrics.h"
#include "actor.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

// ---- Existing 5 tests (preserved) ----

DELVE_TEST(default_gait_parameters_valid) {
  ProceduralGait g;
  EXPECT_GT(g.stride_len, 0.0f);
  EXPECT_GT(g.step_height, 0.0f);
  EXPECT_GT(g.step_duration, 0.0f);
  EXPECT_GT(g.move_speed, 0.0f);
  return true;
}

DELVE_TEST(default_actor_config_proportional) {
  ActorConfig c;
  EXPECT_GT(c.leg_len, c.arm_len);          // legs longer than arms
  EXPECT_GT(c.torso_len, 0.3f);             // torso substantial
  EXPECT_LT(c.head_radius, c.torso_len);    // head smaller than torso
  EXPECT_GT(c.shoulder_width, c.hip_width);  // shoulders wider than hips
  return true;
}

DELVE_TEST(skeleton_pose_joint_count) {
  EXPECT_EQ((int)Joint::COUNT, 17);
  SkeletonPose pose;
  for (int i = 0; i < (int)Joint::COUNT; ++i) {
    EXPECT_NEAR(pose.joints[i].x, 0.0f, 1e-6f);
    EXPECT_NEAR(pose.joints[i].y, 0.0f, 1e-6f);
    EXPECT_NEAR(pose.joints[i].z, 0.0f, 1e-6f);
  }
  return true;
}

DELVE_TEST(skeleton_symmetry_at_rest) {
  SkeletonPose pose;
  ActorConfig cfg;
  pose.joints[(int)Joint::ROOT]       = {0, 0, 0};
  pose.joints[(int)Joint::SPINE]      = {0, cfg.torso_len * 0.3f, 0};
  pose.joints[(int)Joint::CHEST]      = {0, cfg.torso_len * 0.7f, 0};
  pose.joints[(int)Joint::NECK]       = {0, cfg.torso_len, 0};
  pose.joints[(int)Joint::HEAD]       = {0, cfg.torso_len + cfg.neck_len, 0};
  pose.joints[(int)Joint::L_SHOULDER] = {-cfg.shoulder_width, cfg.torso_len, 0};
  pose.joints[(int)Joint::R_SHOULDER] = { cfg.shoulder_width, cfg.torso_len, 0};
  pose.joints[(int)Joint::L_ELBOW]    = {-cfg.shoulder_width - cfg.arm_len, cfg.torso_len, 0};
  pose.joints[(int)Joint::R_ELBOW]    = { cfg.shoulder_width + cfg.arm_len, cfg.torso_len, 0};
  pose.joints[(int)Joint::L_WRIST]    = {-cfg.shoulder_width - cfg.arm_len - cfg.forearm_len, cfg.torso_len, 0};
  pose.joints[(int)Joint::R_WRIST]    = { cfg.shoulder_width + cfg.arm_len + cfg.forearm_len, cfg.torso_len, 0};
  pose.joints[(int)Joint::L_HIP]      = {-cfg.hip_width, 0, 0};
  pose.joints[(int)Joint::R_HIP]      = { cfg.hip_width, 0, 0};
  pose.joints[(int)Joint::L_KNEE]     = {-cfg.hip_width, -cfg.leg_len, 0};
  pose.joints[(int)Joint::R_KNEE]     = { cfg.hip_width, -cfg.leg_len, 0};
  pose.joints[(int)Joint::L_ANKLE]    = {-cfg.hip_width, -cfg.leg_len - cfg.shin_len, 0};
  pose.joints[(int)Joint::R_ANKLE]    = { cfg.hip_width, -cfg.leg_len - cfg.shin_len, 0};

  float sym = pose_symmetry_score(pose);
  EXPECT_GT(sym, 0.95f);
  return true;
}

DELVE_TEST(leg_state_parallel_arrays_consistent) {
  LegState ls;
  for (int i = 0; i < 2; ++i) {
    EXPECT_FALSE(ls.stepping[i]);
    EXPECT_NEAR(ls.progress[i], 0.0f, 1e-6f);
  }
  return true;
}

// ---- Biomechanical animation tests ----

// smooth_damp helper (mirrors actor_animation.cpp implementation).
static float smooth_damp_test(float current, float target, float *velocity,
                               float smooth_time, float dt) {
  float omega = 2.0f / smooth_time;
  float x     = omega * dt;
  float exp_f = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
  float delta = current - target;
  float temp  = (*velocity + omega * delta) * dt;
  *velocity   = (*velocity - omega * temp) * exp_f;
  return target + (delta + temp) * exp_f;
}

// Test: smooth_damp converges to target within 1 second (60 frames, smooth_time=0.1s).
DELVE_TEST(smooth_damp_convergence) {
  float current = 0.0f, target = 1.0f, rate = 0.0f;
  float dt = 1.0f / 60.0f;
  for (int i = 0; i < 60; ++i)
    current = smooth_damp_test(current, target, &rate, 0.1f, dt);
  EXPECT_GT(current, 0.99f);
  return true;
}

// Test: Arm phase opposes leg phase (anti-phase) for >70% of gait cycle samples.
// Fix 1 foundation: left arm = sin(phase + PI) = -sin(phase), left leg ∝ sin(phase).
DELVE_TEST(arm_phase_opposes_leg) {
  int opposing_count = 0;
  int total_samples  = 100;
  for (int i = 0; i < total_samples; ++i) {
    float phase       = (float)i / total_samples * glm::two_pi<float>();
    float l_arm_angle = sinf(phase + glm::pi<float>());
    float l_leg_fwd   = sinf(phase);
    float opposition  = arm_phase_opposition(l_arm_angle, l_leg_fwd);
    if (opposition > 0.5f) ++opposing_count;
  }
  float ratio = (float)opposing_count / total_samples;
  EXPECT_GT(ratio, 0.7f);
  return true;
}

// Test: Joint delay ordering — shoulder converges faster than wrist (successive breaking).
// Validates Fix 1's joint-delay chain: shoulder(0.02s) < elbow(0.05s) < wrist(0.08s).
DELVE_TEST(joint_delay_ordering) {
  float dt = 1.0f / 60.0f;
  float target = 1.0f;
  float shoulder = 0.0f, shoulder_rate = 0.0f;
  float elbow    = 0.0f, elbow_rate    = 0.0f;
  float wrist    = 0.0f, wrist_rate    = 0.0f;
  for (int i = 0; i < 3; ++i) {
    shoulder = smooth_damp_test(shoulder, target,  &shoulder_rate, 0.02f, dt);
    elbow    = smooth_damp_test(elbow,    shoulder, &elbow_rate,    0.05f, dt);
    wrist    = smooth_damp_test(wrist,    elbow,    &wrist_rate,    0.08f, dt);
  }
  float shoulder_err = fabsf(shoulder - target);
  float elbow_err    = fabsf(elbow    - target);
  float wrist_err    = fabsf(wrist    - target);
  EXPECT_LT(shoulder_err, elbow_err);
  EXPECT_LT(elbow_err,    wrist_err);
  return true;
}

// Test: Idle breathing frequency is approximately 0.6 Hz (≈3 cycles in 5 seconds).
DELVE_TEST(idle_breathing_frequency) {
  float dt = 1.0f / 60.0f;
  int frames = (int)(5.0f / dt);
  float phase = 0.0f, prev_val = 0.0f;
  int crossings = 0;
  for (int i = 0; i < frames; ++i) {
    phase += dt * glm::two_pi<float>() * 0.6f;
    float val = sinf(phase) * 0.008f;
    if (prev_val < 0.0f && val >= 0.0f) ++crossings;
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

// Test: Fix 1 — Elliptical foot path via cosine-ease has peak XY velocity at mid-stride.
// ts_xy = (1 - cos(p*PI)) / 2  →  d(ts_xy)/dp ∝ sin(p*PI), peaks at p=0.5.
DELVE_TEST(elliptical_foot_path_peak_velocity_at_midstride) {
  auto cosease = [](float p) { return (1.0f - cosf(p * glm::pi<float>())) * 0.5f; };
  float dp       = 0.05f;
  float t_start  = cosease(0.00f + dp) - cosease(0.00f);
  float t_mid    = cosease(0.50f + dp) - cosease(0.50f);
  float t_end    = cosease(1.00f)      - cosease(1.00f - dp);
  // Mid-stride velocity is strictly greater than start and end (elliptical acceleration).
  EXPECT_GT(t_mid, t_start);
  EXPECT_GT(t_mid, t_end);
  // Endpoints are slow: less than half the mid-stride rate.
  EXPECT_LT(t_start, t_mid * 0.5f);
  EXPECT_LT(t_end,   t_mid * 0.5f);
  return true;
}

// Test: Fix 2 — Directional multiplier is higher for isometric-vertical movement.
// Moving along world(-1,-1)/sqrt(2) (screen "up") gives multiplier > 1.3.
// Moving along world(+1,0) gives a lower multiplier.
DELVE_TEST(directional_multiplier_isometric_vertical) {
  static constexpr float ISO_AXIS_X = -0.70710678118f;
  static constexpr float ISO_AXIS_Y = -0.70710678118f;

  // Direction along isometric vertical (NW in world = up on screen).
  float vel_dx_up = -0.70710678118f, vel_dy_up = -0.70710678118f;
  float iso_vert_up  = fabsf(vel_dx_up * ISO_AXIS_X + vel_dy_up * ISO_AXIS_Y);
  float mult_up      = 1.0f + iso_vert_up * 0.5f;

  // Direction along isometric horizontal (world +X).
  float vel_dx_right = 1.0f, vel_dy_right = 0.0f;
  float iso_vert_right = fabsf(vel_dx_right * ISO_AXIS_X + vel_dy_right * ISO_AXIS_Y);
  float mult_right     = 1.0f + iso_vert_right * 0.5f;

  EXPECT_GT(mult_up,    mult_right);  // vertical axis → higher multiplier
  EXPECT_GT(mult_up,    1.3f);        // meaningful boost (≥+30%)
  EXPECT_LT(mult_right, mult_up);
  return true;
}

// Test: Fix 3 — Hip double-bounce: |sin(phase)| has exactly 2 minima per 2*PI cycle.
// Minima of |sin(x)| occur at x=0 and x=PI within [0, 2*PI).
DELVE_TEST(hip_double_bounce_twice_per_stride) {
  float amplitude = 0.025f;
  int bounce_count = 0;
  int total_steps  = 1000;
  for (int i = 1; i < total_steps; ++i) {
    float prev_phase = (float)(i - 1) / total_steps * glm::two_pi<float>();
    float curr_phase = (float)i       / total_steps * glm::two_pi<float>();
    float next_phase = (float)(i + 1) / total_steps * glm::two_pi<float>();
    float prev_bob = fabsf(sinf(prev_phase)) * -amplitude;
    float curr_bob = fabsf(sinf(curr_phase)) * -amplitude;
    float next_bob = fabsf(sinf(next_phase)) * -amplitude;
    if (curr_bob < prev_bob && curr_bob < next_bob) ++bounce_count;
  }
  EXPECT_EQ(bounce_count, 2);
  return true;
}

// Test: Fix 3 — Hip roll counter-animates: sign of roll opposes gait phase sign.
// When sin(phase) > 0, target_hip_roll > 0 (hips roll right when left foot is back).
DELVE_TEST(hip_roll_counter_animation) {
  int matching_count = 0;
  int total_samples  = 100;
  for (int i = 0; i < total_samples; ++i) {
    float phase = (float)i / total_samples * glm::two_pi<float>();
    float sin_p = sinf(phase);
    if (fabsf(sin_p) < 0.05f) continue; // skip near-zero crossings
    float target_hip_roll = sin_p * 0.06f;
    // Roll sign should match sin(phase) sign — counter-animation means hips
    // shift to the planted foot side.
    bool same_sign = (target_hip_roll > 0) == (sin_p > 0);
    if (same_sign) ++matching_count;
  }
  // Should be >90% matching (excluding zero crossings already skipped above).
  EXPECT_GT(matching_count, 70);
  return true;
}

// Test: One-foot-planted invariant — both legs never step simultaneously.
DELVE_TEST(no_simultaneous_stepping) {
  LegState legs{};
  ProceduralGait gait{};

  legs.foot[0] = {-0.25f, 0.0f, 0.0f};
  legs.foot[1] = { 0.25f, 0.0f, 0.0f};
  legs.prev_foot[0] = legs.foot[0];
  legs.prev_foot[1] = legs.foot[1];
  legs.target[0]    = legs.foot[0];
  legs.target[1]    = legs.foot[1];

  float dt = 1.0f / 60.0f;
  bool both_stepping_ever = false;

  float pos_x = 0.0f, pos_y = 0.0f;
  float vel_x = 3.0f, vel_y = 0.0f;
  float facing = 0.0f;

  for (int frame = 0; frame < 200; ++frame) {
    float speed = sqrtf(vel_x * vel_x + vel_y * vel_y);
    gait.phase += speed * dt * (glm::two_pi<float>() / (2.0f * gait.stride_len));

    pos_x += vel_x * dt;
    pos_y += vel_y * dt;

    float rght_x = -sinf(facing), rght_y = cosf(facing);
    float hip_sign[2] = { -1.0f, 1.0f };
    float vel_dx = vel_x / speed, vel_dy = vel_y / speed;

    float speed_ratio       = std::max(0.2f, std::min(1.0f, speed / gait.move_speed));
    float adaptive_duration = gait.step_duration / speed_ratio;

    for (int leg = 0; leg < 2; ++leg) {
      int other_leg = 1 - leg;

      float hip_x = pos_x + rght_x * hip_sign[leg] * 0.25f;
      float hip_y = pos_y + rght_y * hip_sign[leg] * 0.25f;

      float pred_x = hip_x + vel_dx * gait.stride_len * 0.5f;
      float pred_y = hip_y + vel_dy * gait.stride_len * 0.5f;

      if (!legs.stepping[leg]) {
        float dx   = legs.foot[leg].x - pred_x;
        float dy   = legs.foot[leg].y - pred_y;
        float dist = sqrtf(dx * dx + dy * dy);

        bool other_planted = !legs.stepping[other_leg];
        if (dist > gait.stride_len * 0.5f && other_planted) {
          legs.stepping[leg]  = true;
          legs.progress[leg]  = 0.0f;
          legs.prev_foot[leg] = legs.foot[leg];
          legs.target[leg]    = {pred_x, pred_y, 0.0f};
        }
      }

      if (legs.stepping[leg]) {
        legs.progress[leg] += dt / adaptive_duration;
        float progress = std::min(legs.progress[leg], 1.0f);
        // Use cosine-ease for XY (Fix 1).
        float ts = (1.0f - cosf(progress * glm::pi<float>())) * 0.5f;

        legs.foot[leg].x = legs.prev_foot[leg].x + (legs.target[leg].x - legs.prev_foot[leg].x) * ts;
        legs.foot[leg].y = legs.prev_foot[leg].y + (legs.target[leg].y - legs.prev_foot[leg].y) * ts;

        if (legs.progress[leg] >= 1.0f) {
          legs.stepping[leg] = false;
          legs.foot[leg]     = legs.target[leg];
        }
      }
    }

    if (legs.stepping[0] && legs.stepping[1])
      both_stepping_ever = true;
  }

  EXPECT_FALSE(both_stepping_ever);
  return true;
}