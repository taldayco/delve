#include "test_harness.h"
#include "animation_metrics.h"
#include "actor.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

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

// ---------------------------------------------------------------------------
// New fluid animation tests
// ---------------------------------------------------------------------------

DELVE_TEST(smooth_damp_converges_to_target) {
  // Run SmoothDamp from 0 to 10 for 100 frames at 60 Hz
  // With smoothing_time=0.12f it should be >99% converged by ~0.5s (30 frames)
  float residual = smooth_damp_residual(0.0f, 10.0f, 0.12f, 1.0f / 60.0f, 100);
  // Residual should be < 1% after 100 frames (~1.67s)
  EXPECT_LT(residual, 0.01f);
  return true;
}

DELVE_TEST(smooth_damp_single_frame_is_gradual) {
  // In one frame at 60 Hz with smoothing_time=0.12f,
  // should move less than 50% toward target (smooth, not instant)
  float fraction = smooth_damp_single_frame_fraction(0.12f, 1.0f / 60.0f);
  EXPECT_LT(fraction, 0.5f);
  EXPECT_GT(fraction, 0.001f); // but should move at least a little
  return true;
}

DELVE_TEST(arm_phases_default_antiphase) {
  // Default AnimationState should have arm_phase[0]=0, arm_phase[1]=pi (antiphase)
  AnimationState anim;
  EXPECT_NEAR(anim.arm_phase[0], 0.0f, 1e-4f);
  EXPECT_NEAR(anim.arm_phase[1], glm::pi<float>(), 1e-4f);
  EXPECT_TRUE(arm_phases_antiphase(anim.arm_phase[0], anim.arm_phase[1], 0.1f));
  return true;
}

DELVE_TEST(breathing_amplitude_in_range) {
  // The breathing amplitude of 0.012f should be in valid range
  EXPECT_TRUE(breathing_amplitude_valid(0.012f));
  // Too large amplitude (0.5f) should be invalid
  EXPECT_FALSE(breathing_amplitude_valid(0.5f));
  // Too small (near zero) should be invalid
  EXPECT_FALSE(breathing_amplitude_valid(0.0001f));
  return true;
}

DELVE_TEST(adaptive_step_duration_decreases_with_speed) {
  // At zero speed: slowest duration (0.45s)
  float dur_stopped  = adaptive_step_duration(0.0f,  4.0f);
  // At half speed: intermediate
  float dur_half     = adaptive_step_duration(2.0f,  4.0f);
  // At full speed: fastest duration (0.22s)
  float dur_full     = adaptive_step_duration(4.0f,  4.0f);

  EXPECT_NEAR(dur_stopped, 0.45f, 1e-4f);
  EXPECT_NEAR(dur_full,    0.22f, 1e-4f);
  EXPECT_GT(dur_stopped, dur_half);
  EXPECT_GT(dur_half,    dur_full);
  return true;
}

DELVE_TEST(foot_planted_invariant) {
  // Both feet planted: invariant holds
  LegState ls_planted;
  EXPECT_TRUE(foot_planted_invariant_holds(ls_planted));

  // Left foot stepping, right planted: invariant holds
  LegState ls_left_step;
  ls_left_step.stepping[0] = true;
  ls_left_step.stepping[1] = false;
  EXPECT_TRUE(foot_planted_invariant_holds(ls_left_step));

  // Both stepping: invariant VIOLATED
  LegState ls_both;
  ls_both.stepping[0] = true;
  ls_both.stepping[1] = true;
  EXPECT_FALSE(foot_planted_invariant_holds(ls_both));
  return true;
}

DELVE_TEST(animation_state_default_initialized) {
  AnimationState anim;
  // Velocity fields zero
  EXPECT_NEAR(anim.smooth_vel.x,      0.0f, 1e-6f);
  EXPECT_NEAR(anim.smooth_vel.y,      0.0f, 1e-6f);
  EXPECT_NEAR(anim.vel_vel.x,         0.0f, 1e-6f);
  EXPECT_NEAR(anim.vel_vel.y,         0.0f, 1e-6f);
  EXPECT_NEAR(anim.prev_smooth_vel.x, 0.0f, 1e-6f);
  EXPECT_NEAR(anim.prev_smooth_vel.y, 0.0f, 1e-6f);
  // Sway amount initialized
  EXPECT_NEAR(anim.sway_amt, 0.04f, 1e-4f);
  // Phases zero
  EXPECT_NEAR(anim.breath_phase,  0.0f, 1e-6f);
  EXPECT_NEAR(anim.weight_phase,  0.0f, 1e-6f);
  EXPECT_NEAR(anim.sway_phase,    0.0f, 1e-6f);
  // arm_phase[1] is pi (antiphase initialization)
  EXPECT_NEAR(anim.arm_phase[1], glm::pi<float>(), 1e-4f);
  return true;
}
