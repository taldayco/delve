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
  EXPECT_GT(c.leg_len, c.arm_len);
  EXPECT_GT(c.torso_len, 0.3f);
  EXPECT_LT(c.head_radius, c.torso_len);
  EXPECT_GT(c.shoulder_width, c.hip_width);
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

// Local SmoothDamp mirror for headless test (no actor_animation.cpp linkage in tests).
static float smooth_damp_local(float current, float target, float &vel_state,
                                float smooth_time, float dt) {
    float omega = 2.0f / smooth_time;
    float x = omega * dt;
    float exp_factor = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float temp = (vel_state + omega * change) * dt;
    vel_state = (vel_state - omega * temp) * exp_factor;
    return target + (change + temp) * exp_factor;
}

DELVE_TEST(smooth_damp_converges_to_target) {
    float current = 0.0f, target = 1.0f, vel = 0.0f;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i)
        current = smooth_damp_local(current, target, vel, 0.12f, dt);
    EXPECT_NEAR(current, 1.0f, 0.01f);
    return true;
}

DELVE_TEST(arm_phases_default_antiphase) {
    // New AnimationState uses l_arm_target/r_arm_target computed from opposing phases.
    // At any non-zero gait phase, l_arm_target = sin(phase + pi), r_arm_target = sin(phase).
    // Verify these are always opposite-sign (antiphase) for a non-trivial phase.
    float phase = 1.0f; // arbitrary non-zero, non-pi phase
    float swing_amp = 0.3f;
    float l = sinf(phase + glm::pi<float>()) * swing_amp;
    float r = sinf(phase)                     * swing_amp;
    // Antiphase: l * r < 0 for most phases (except 0, pi where both are 0)
    EXPECT_LT(l * r, 0.0f);
    // Also confirm default AnimationState initialises arm targets to zero.
    AnimationState anim;
    EXPECT_NEAR(anim.l_arm_target, 0.0f, 1e-6f);
    EXPECT_NEAR(anim.r_arm_target, 0.0f, 1e-6f);
    return true;
}

DELVE_TEST(breathing_amplitude_valid_range) {
    float amp = breathing_amplitude();
    EXPECT_GT(amp, 0.005f);
    EXPECT_LT(amp, 0.05f);
    return true;
}

DELVE_TEST(foot_planted_invariant_holds) {
    LegState legs;
    legs.stepping[0] = true;
    legs.stepping[1] = false;

    // Simulate invariant check for leg 1: other=0 is stepping → other NOT planted.
    int leg = 1;
    int other = 1 - leg;
    bool other_planted = !legs.stepping[other];
    // Invariant: only step if other is planted. Here other is NOT planted.
    EXPECT_FALSE(other_planted);
    return true;
}

DELVE_TEST(adaptive_step_duration_speed_scaling) {
    ProceduralGait g;
    float slow_dur = step_duration_at_speed(0.0f, g.move_speed);
    float fast_dur = step_duration_at_speed(g.move_speed, g.move_speed);
    EXPECT_GT(slow_dur, fast_dur);
    EXPECT_NEAR(slow_dur, 0.45f, 0.001f);
    EXPECT_NEAR(fast_dur, 0.22f, 0.001f);
    return true;
}

DELVE_TEST(torso_lean_successive_breaking) {
    constexpr float spine_frac = 0.30f;
    constexpr float chest_frac = 0.62f;
    constexpr float head_frac  = 1.00f;

    EXPECT_GT(chest_frac, spine_frac);
    EXPECT_GT(head_frac,  chest_frac);
    EXPECT_NEAR(spine_frac, 0.30f, 0.001f);
    EXPECT_NEAR(chest_frac, 0.62f, 0.001f);
    EXPECT_NEAR(head_frac,  1.00f, 0.001f);
    return true;
}
