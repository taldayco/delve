#include "test_harness.h"
#include "animation_metrics.h"
#include "actor.h"
#include <glm/glm.hpp>

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

DELVE_TEST(smooth_velocity_convergence) {
  // Critically-damped spring from 0 to 1 with smooth_time=0.2s at 60Hz
  // should converge well within 600 steps
  int steps = smooth_velocity_convergence(0.0f, 1.0f, 0.2f, 1.0f / 60.0f);
  EXPECT_GT(steps, 0);    // must converge (not -1)
  EXPECT_LT(steps, 600);  // must converge before the cap
  return true;
}

DELVE_TEST(torso_lean_proportional) {
  // lean ~ speed * lean_factor; faster speed produces greater lean magnitude
  const float lean_factor = 0.04f;
  float slow_speed = 1.0f;
  float fast_speed = 6.0f;
  float lean_slow = slow_speed * lean_factor;
  float lean_fast = fast_speed * lean_factor;
  EXPECT_TRUE(torso_lean_proportional(lean_slow, lean_fast));
  return true;
}

DELVE_TEST(arm_swing_antiphase) {
  constexpr float pi = 3.14159265f;
  // Left at 0.0, right at π — exactly antiphase in radians
  EXPECT_TRUE(arm_swing_antiphase(0.0f, pi));
  // Phases within tolerance also pass (e.g. slight drift after some frames)
  EXPECT_TRUE(arm_swing_antiphase(0.1f, pi + 0.1f));
  // Same phase is NOT antiphase
  EXPECT_FALSE(arm_swing_antiphase(0.0f, 0.0f));
  // Offset by only 0.5 rad (~28°) is NOT antiphase
  EXPECT_FALSE(arm_swing_antiphase(0.0f, 0.5f));
  return true;
}

DELVE_TEST(breathing_amplitude_reasonable) {
  EXPECT_TRUE(breathing_amplitude_reasonable(0.012f));  // nominal value
  EXPECT_FALSE(breathing_amplitude_reasonable(0.0f));   // zero — no breath
  EXPECT_FALSE(breathing_amplitude_reasonable(0.1f));   // too large
  return true;
}

DELVE_TEST(foot_planted_invariant) {
  // Both feet stepping simultaneously is invalid
  LegState ls;
  ls.stepping[0] = true;
  ls.stepping[1] = true;
  EXPECT_FALSE(foot_planted_invariant(ls));

  // One foot stepping is valid
  ls.stepping[1] = false;
  EXPECT_TRUE(foot_planted_invariant(ls));

  // Neither stepping (standing still) is also valid
  ls.stepping[0] = false;
  EXPECT_TRUE(foot_planted_invariant(ls));
  return true;
}

DELVE_TEST(step_duration_adaptive) {
  // Faster speed should produce shorter step duration
  // dur = mix(0.45, 0.22, t_norm) where t_norm = clamp(speed/max_speed, 0, 1)
  const float dur_min  = 0.22f;
  const float dur_max  = 0.45f;
  const float max_spd  = 8.0f;

  float t_slow = 1.0f / max_spd;  // speed=1
  float t_fast = 6.0f / max_spd;  // speed=6
  float dur_slow = dur_max + t_slow * (dur_min - dur_max);
  float dur_fast = dur_max + t_fast * (dur_min - dur_max);

  EXPECT_TRUE(step_duration_adaptive(dur_slow, dur_fast));
  return true;
}
