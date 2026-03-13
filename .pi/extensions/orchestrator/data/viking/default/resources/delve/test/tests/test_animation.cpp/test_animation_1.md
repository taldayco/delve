#include "rig.h"
#include "animation_metrics.h"
#include "config.h"
#include "test_harness.h"
#include "terrain/map_util.h"
#include "terrain/map_data.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// ---- Existing 5 tests (preserved) + height proportions ----

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
  EXPECT_EQ((int)Joint::COUNT, 32);
  RigPose pose;
  for (int i = 0; i < (int)Joint::COUNT; ++i) {
    EXPECT_NEAR(pose.joints[i].x, 0.0f, 1e-6f);
    EXPECT_NEAR(pose.joints[i].y, 0.0f, 1e-6f);
    EXPECT_NEAR(pose.joints[i].z, 0.0f, 1e-6f);
  }
  return true;
}

DELVE_TEST(skeleton_symmetry_at_rest) {
  RigPose pose;
  ActorConfig cfg;
  pose.joints[(int)Joint::HIPS]     = {0, 0, 0};
  pose.joints[(int)Joint::SPINE_01] = {0, 0, cfg.torso_len * 0.3f};
  pose.joints[(int)Joint::CHEST]    = {0, 0, cfg.torso_len * 0.7f};
  pose.joints[(int)Joint::NECK]     = {0, 0, cfg.torso_len};
  pose.joints[(int)Joint::HEAD]     = {0, 0, cfg.torso_len + cfg.neck_len};
  pose.joints[(int)Joint::L_UPPER_ARM] = {-cfg.shoulder_width, 0, cfg.torso_len};
  pose.joints[(int)Joint::R_UPPER_ARM] = {cfg.shoulder_width, 0, cfg.torso_len};
  pose.joints[(int)Joint::L_LOWER_ARM] = {-cfg.shoulder_width - cfg.arm_len,
                                          0, cfg.torso_len};
  pose.joints[(int)Joint::R_LOWER_ARM] = {cfg.shoulder_width + cfg.arm_len,
                                          0, cfg.torso_len};
  pose.joints[(int)Joint::L_HAND] = {
      -cfg.shoulder_width - cfg.arm_len - cfg.forearm_len, 0, cfg.torso_len};
  pose.joints[(int)Joint::R_HAND] = {
      cfg.shoulder_width + cfg.arm_len + cfg.forearm_len, 0, cfg.torso_len};
  pose.joints[(int)Joint::L_UPPER_LEG] = {-cfg.hip_width, 0, 0};
  pose.joints[(int)Joint::R_UPPER_LEG] = {cfg.hip_width, 0, 0};
  pose.joints[(int)Joint::L_LOWER_LEG] = {-cfg.hip_width, 0, -cfg.leg_len};
  pose.joints[(int)Joint::R_LOWER_LEG] = {cfg.hip_width, 0, -cfg.leg_len};
  pose.joints[(int)Joint::L_FOOT] = {-cfg.hip_width,
                                     0, -cfg.leg_len - cfg.shin_len};
  pose.joints[(int)Joint::R_FOOT] = {cfg.hip_width,
                                     0, -cfg.leg_len - cfg.shin_len};

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

// smooth_damp helper (mirrors rig_animation.cpp implementation).
static float smooth_damp_test(float current, float target, float *velocity,
                              float smooth_time, float dt) {
  float omega = 2.0f / smooth_time;
  float x = omega * dt;
  float exp_f = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
  float delta = current - target;
  float temp = (*velocity + omega * delta) * dt;
  *velocity = (*velocity - omega * temp) * exp_f;
  return target + (delta + temp) * exp_f;
}

// Test: smooth_damp converges to target within 1 second (60 frames,
// smooth_time=0.1s).
DELVE_TEST(smooth_damp_convergence) {
  float current = 0.0f, target = 1.0f, rate = 0.0f;
  float dt = 1.0f / 60.0f;
  for (int i = 0; i < 60; ++i)
    current = smooth_damp_test(current, target, &rate, 0.1f, dt);
  EXPECT_GT(current, 0.99f);
  return true;
}