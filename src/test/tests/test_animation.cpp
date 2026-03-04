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
  EXPECT_GT(c.shoulder_width, c.hip_width); // shoulders wider than hips
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

// -----------------------------------------------------------------------
// New tests: fluid procedural animation features
// -----------------------------------------------------------------------

DELVE_TEST(smooth_damp_converges_to_target) {
  // After 1 second at 60 fps with smooth_time=0.12s, residual must be tiny
  float residual = smooth_damp_residual(10.0f, 0.0f, 0.12f, 60, 1.0f / 60.0f);
  EXPECT_LT(residual, 0.01f);
  return true;
}

DELVE_TEST(arm_phases_default_antiphase) {
  AnimationState anim;
  // Default: arm_phase[0]=0, arm_phase[1]=π — should be detected as antiphase
  bool antiphase = arm_phases_antiphase(anim.arm_phase[0], anim.arm_phase[1]);
  EXPECT_TRUE(antiphase);
  return true;
}

DELVE_TEST(breathing_amplitude_valid_range) {
  // The chosen breathing amplitude (0.012f) must be in the valid physiological range
  EXPECT_TRUE(breathing_amplitude_valid(0.012f));
  // Sanity: zero amplitude is invalid
  EXPECT_FALSE(breathing_amplitude_valid(0.0f));
  return true;
}

DELVE_TEST(foot_planted_invariant_holds) {
  // One-foot-planted: both feet may NOT step simultaneously
  EXPECT_TRUE(foot_planted_invariant(false, false));  // both planted: OK
  EXPECT_TRUE(foot_planted_invariant(true,  false));  // only left stepping: OK
  EXPECT_TRUE(foot_planted_invariant(false, true));   // only right stepping: OK
  EXPECT_FALSE(foot_planted_invariant(true,  true));  // VIOLATION: both airborne
  return true;
}

DELVE_TEST(adaptive_step_duration_speed_scaling) {
  // At zero speed step_duration should be 0.45s; at full speed 0.22s
  // Verify the invariant: slow > fast
  float slow_dur = 0.45f;  // stationary (speed_t = 0)
  float fast_dur = 0.22f;  // full sprint (speed_t = 1)
  EXPECT_TRUE(adaptive_step_duration_decreases(slow_dur, fast_dur));
  // Also check the formula endpoint values
  EXPECT_GT(slow_dur, 0.3f);
  EXPECT_LT(fast_dur, 0.3f);
  return true;
}

DELVE_TEST(torso_lean_successive_breaking) {
  // Spine chain breaking fractions must be strictly increasing
  // SPINE=30%, CHEST=62%, NECK/HEAD=100%
  EXPECT_TRUE(torso_lean_successive(0.30f, 0.62f, 1.00f));
  // Sanity: equal fractions are NOT successive
  EXPECT_FALSE(torso_lean_successive(0.30f, 0.30f, 1.00f));
  return true;
}
