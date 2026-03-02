#include "test_harness.h"
#include "animation_metrics.h"

DELVE_TEST(default_gait_parameters_valid) {
  ProceduralGait gait;
  EXPECT_GT(gait.stride_len, 0.0f);
  EXPECT_GT(gait.step_height, 0.0f);
  EXPECT_GT(gait.step_duration, 0.0f);
  EXPECT_GT(gait.move_speed, 0.0f);
}

DELVE_TEST(default_actor_config_proportional) {
  ActorConfig config;
  // Legs should be longer than arms
  float total_leg = config.leg_len + config.shin_len;
  float total_arm = config.arm_len + config.forearm_len;
  EXPECT_GT(total_leg, total_arm);

  // Torso should be substantial
  EXPECT_GT(config.torso_len, 0.3f);

  // Head should be smaller than torso
  EXPECT_LT(config.head_radius * 2, config.torso_len);

  // Shoulders wider than hips
  EXPECT_GT(config.shoulder_width, config.hip_width);
}

DELVE_TEST(skeleton_pose_joint_count) {
  EXPECT_EQ((int)Joint::COUNT, 17);

  SkeletonPose pose;
  // All joints should initialize to zero
  for (int i = 0; i < (int)Joint::COUNT; i++) {
    EXPECT_NEAR(glm::length(pose.joints[i]), 0.0f, 1e-6f);
  }
}

DELVE_TEST(skeleton_symmetry_at_rest) {
  // Build a T-pose skeleton manually
  SkeletonPose pose;
  ActorConfig cfg;

  // Spine chain (vertical, Y-up)
  pose.joints[(int)Joint::ROOT]  = glm::vec3(0, 0, 0);
  pose.joints[(int)Joint::SPINE] = glm::vec3(0, cfg.torso_len * 0.3f, 0);
  pose.joints[(int)Joint::CHEST] = glm::vec3(0, cfg.torso_len * 0.7f, 0);
  pose.joints[(int)Joint::NECK]  = glm::vec3(0, cfg.torso_len, 0);
  pose.joints[(int)Joint::HEAD]  = glm::vec3(0, cfg.torso_len + cfg.neck_len, 0);

  // Symmetric arms
  float chest_y = cfg.torso_len * 0.7f;
  pose.joints[(int)Joint::L_SHOULDER] = glm::vec3(-cfg.shoulder_width, chest_y, 0);
  pose.joints[(int)Joint::R_SHOULDER] = glm::vec3( cfg.shoulder_width, chest_y, 0);
  pose.joints[(int)Joint::L_ELBOW]    = glm::vec3(-cfg.shoulder_width - cfg.arm_len, chest_y, 0);
  pose.joints[(int)Joint::R_ELBOW]    = glm::vec3( cfg.shoulder_width + cfg.arm_len, chest_y, 0);
  pose.joints[(int)Joint::L_WRIST]    = glm::vec3(-cfg.shoulder_width - cfg.arm_len - cfg.forearm_len, chest_y, 0);
  pose.joints[(int)Joint::R_WRIST]    = glm::vec3( cfg.shoulder_width + cfg.arm_len + cfg.forearm_len, chest_y, 0);

  // Symmetric legs
  pose.joints[(int)Joint::L_HIP]   = glm::vec3(-cfg.hip_width, 0, 0);
  pose.joints[(int)Joint::R_HIP]   = glm::vec3( cfg.hip_width, 0, 0);
  pose.joints[(int)Joint::L_KNEE]  = glm::vec3(-cfg.hip_width, -cfg.leg_len, 0);
  pose.joints[(int)Joint::R_KNEE]  = glm::vec3( cfg.hip_width, -cfg.leg_len, 0);
  pose.joints[(int)Joint::L_ANKLE] = glm::vec3(-cfg.hip_width, -(cfg.leg_len + cfg.shin_len), 0);
  pose.joints[(int)Joint::R_ANKLE] = glm::vec3( cfg.hip_width, -(cfg.leg_len + cfg.shin_len), 0);

  float sym = pose_symmetry_score(pose);
  EXPECT_GT(sym, 0.95f);
}

DELVE_TEST(leg_state_parallel_arrays_consistent) {
  LegState state;
  // Both legs should start non-stepping
  EXPECT_FALSE(state.stepping[0]);
  EXPECT_FALSE(state.stepping[1]);
  EXPECT_NEAR(state.progress[0], 0.0f, 1e-6f);
  EXPECT_NEAR(state.progress[1], 0.0f, 1e-6f);
}
