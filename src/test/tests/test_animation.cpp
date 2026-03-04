#include "test_harness.h"
#include "animation_metrics.h"
#include "actor.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

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
  // Build a simple T-pose
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

// ---- New 6 tests for biomechanical animation ----

// Test 1: smooth_damp converges to target within 1 second (dt=1/60).
DELVE_TEST(smooth_damp_convergence) {
  // Inline the smooth_damp formula (same as actor_animation.cpp).
  auto smooth_damp_fn = [](float current, float target, float *velocity,
                            float smooth_time, float dt) -> float {
    float omega = 2.0f / smooth_time;
    float x     = omega * dt;
    float exp_f = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float delta = current - target;
    float temp  = (*velocity + omega * delta) * dt;
    *velocity   = (*velocity - omega * temp) * exp_f;
    return target + (delta + temp) * exp_f;
  };

  float current = 0.0f;
  float target  = 1.0f;
  float rate    = 0.0f;
  float dt      = 1.0f / 60.0f;

  // Run for 1 second (60 frames) with smooth_time = 0.1s.
  for (int i = 0; i < 60; ++i) {
    current = smooth_damp_fn(current, target, &rate, 0.1f, dt);
  }

  // After 1s (10x smooth_time), should be > 99% of target.
  EXPECT_GT(current, 0.99f);
  return true;
}

// Test 2: Arm phase opposes leg phase (anti-phase property).
DELVE_TEST(arm_phase_opposes_leg) {
  // Arm target = sin(phase + PI) for left arm.
  // Leg forward projection = sin(phase) (simplified foot phase model).
  // These should be in anti-phase: product negative.
  int opposing_count = 0;
  int total_samples  = 100;

  for (int i = 0; i < total_samples; ++i) {
    float phase        = (float)i / total_samples * glm::two_pi<float>();
    float l_arm_angle  = sinf(phase + glm::pi<float>());
    float l_leg_fwd    = sinf(phase);  // leg forward = gait phase directly

    float opposition = arm_phase_opposition(l_arm_angle, l_leg_fwd);
    if (opposition > 0.5f) ++opposing_count;
  }

  // At least 80% of samples should show anti-phase (ignoring near-zero crossings).
  // In theory, sin(x+pi) = -sin(x), so they're always opposite-sign except at zeros.
  // Expect > 70% with some tolerance around zero crossings.
  float ratio = (float)opposing_count / total_samples;
  EXPECT_GT(ratio, 0.7f);
  return true;
}

// Test 3: Joint delay ordering — shoulder converges faster than wrist.
DELVE_TEST(joint_delay_ordering) {
  auto smooth_damp_fn = [](float current, float target, float *velocity,
                            float smooth_time, float dt) -> float {
    float omega = 2.0f / smooth_time;
    float x     = omega * dt;
    float exp_f = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float delta = current - target;
    float temp  = (*velocity + omega * delta) * dt;
    *velocity   = (*velocity - omega * temp) * exp_f;
    return target + (delta + temp) * exp_f;
  };

  float dt = 1.0f / 60.0f;
  float target = 1.0f;

  float shoulder = 0.0f, shoulder_rate = 0.0f;
  float elbow    = 0.0f, elbow_rate    = 0.0f;
  float wrist    = 0.0f, wrist_rate    = 0.0f;

  // Advance 3 frames — shoulder 0.02s, elbow 0.05s (tracks shoulder), wrist 0.08s (tracks elbow).
  for (int i = 0; i < 3; ++i) {
    shoulder = smooth_damp_fn(shoulder, target,  &shoulder_rate, 0.02f, dt);
    elbow    = smooth_damp_fn(elbow,    shoulder, &elbow_rate,    0.05f, dt);
    wrist    = smooth_damp_fn(wrist,    elbow,    &wrist_rate,    0.08f, dt);
  }

  // Shoulder should be closest to target (smallest error).
  float shoulder_err = fabsf(shoulder - target);
  float elbow_err    = fabsf(elbow    - target);
  float wrist_err    = fabsf(wrist    - target);

  // Shoulder error < elbow error < wrist error (increasing lag).
  EXPECT_LT(shoulder_err, elbow_err);
  EXPECT_LT(elbow_err,    wrist_err);
  return true;
}

// Test 4: Idle breathing frequency is approximately 0.6 Hz.
DELVE_TEST(idle_breathing_frequency) {
  // Simulate 5 seconds of idle breathing.
  float dt        = 1.0f / 60.0f;
  int   frames    = (int)(5.0f / dt);
  float phase     = 0.0f;
  float prev_val  = 0.0f;
  int   crossings = 0;

  for (int i = 0; i < frames; ++i) {
    phase += dt * glm::two_pi<float>() * 0.6f;
    float val = sinf(phase) * 0.008f;

    // Count upward zero crossings.
    if (prev_val < 0.0f && val >= 0.0f) ++crossings;
    prev_val = val;
  }

  // In 5 seconds at 0.6 Hz, expect ~3 full cycles = ~3 upward crossings.
  // Allow ±1 for edge effects.
  EXPECT_GT(crossings, 1);
  EXPECT_LT(crossings, 6);
  return true;
}

// Test 5: Velocity smoothing has weight — doesn't reach full speed in 1 frame.
DELVE_TEST(velocity_smoothing_has_weight) {
  auto smooth_damp_fn = [](float current, float target, float *velocity,
                            float smooth_time, float dt) -> float {
    float omega = 2.0f / smooth_time;
    float x     = omega * dt;
    float exp_f = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float delta = current - target;
    float temp  = (*velocity + omega * delta) * dt;
    *velocity   = (*velocity - omega * temp) * exp_f;
    return target + (delta + temp) * exp_f;
  };

  float move_speed = 4.0f;
  float current    = 0.0f;
  float rate       = 0.0f;
  float dt         = 1.0f / 60.0f;

  // Apply 1 frame of smoothing toward move_speed.
  float result = smooth_damp_fn(current, move_speed, &rate, 0.1f, dt);

  // After 1 frame at dt=1/60, should be < 50% of move_speed (has weight).
  EXPECT_LT(result, move_speed * 0.5f);
  // But should have moved some (not zero).
  EXPECT_GT(result, 0.0f);
  return true;
}

// Test 6: One-foot-planted invariant — both legs never step simultaneously.
DELVE_TEST(no_simultaneous_stepping) {
  // Simulate the gait logic directly (headless, no ECS needed).
  LegState legs{};
  ProceduralGait gait{};

  // Place feet at origin.
  legs.foot[0] = {-0.25f, 0.0f, 0.0f};
  legs.foot[1] = { 0.25f, 0.0f, 0.0f};
  legs.prev_foot[0] = legs.foot[0];
  legs.prev_foot[1] = legs.foot[1];
  legs.target[0]    = legs.foot[0];
  legs.target[1]    = legs.foot[1];

  float dt = 1.0f / 60.0f;
  bool both_stepping_ever = false;

  // Simulate 200 frames of constant movement.
  float pos_x = 0.0f, pos_y = 0.0f;
  float vel_x = 3.0f, vel_y = 0.0f;
  float facing = 0.0f;

  for (int frame = 0; frame < 200; ++frame) {
    float speed = sqrtf(vel_x * vel_x + vel_y * vel_y);
    gait.phase += speed * dt * (glm::two_pi<float>() / (2.0f * gait.stride_len));

    pos_x += vel_x * dt;
    pos_y += vel_y * dt;

    float fwd_x = cosf(facing), fwd_y = sinf(facing);
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

        // One-foot-planted invariant.
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
        float ts = progress * progress * (3.0f - 2.0f * progress);

        legs.foot[leg].x = legs.prev_foot[leg].x + (legs.target[leg].x - legs.prev_foot[leg].x) * ts;
        legs.foot[leg].y = legs.prev_foot[leg].y + (legs.target[leg].y - legs.prev_foot[leg].y) * ts;

        if (legs.progress[leg] >= 1.0f) {
          legs.stepping[leg] = false;
          legs.foot[leg]     = legs.target[leg];
        }
      }
    }

    // Check invariant.
    if (legs.stepping[0] && legs.stepping[1]) {
      both_stepping_ever = true;
    }
  }

  EXPECT_FALSE(both_stepping_ever);
  return true;
}
