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
