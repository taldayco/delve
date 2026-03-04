#include "test_harness.h"
#include "config.h"
#include "render/skeleton.h"
#include "actor.h"
#include <cmath>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// Tests for the Config::ACTOR_*_WU proportion constants and make_rest_pose().
// ---------------------------------------------------------------------------

// 1. ACTOR_HEIGHT_WU = ACTOR_HEIGHT_FEET * WORLD_UNITS_PER_FOOT = 5.9 * 4 = 23.6
DELVE_TEST(config_actor_height_wu_matches_feet) {
    float expected = Config::ACTOR_HEIGHT_FEET * Config::WORLD_UNITS_PER_FOOT;
    EXPECT_NEAR(Config::ACTOR_HEIGHT_WU, expected, 1e-4f);
    EXPECT_NEAR(Config::ACTOR_TOTAL_HEIGHT_WU, Config::ACTOR_HEIGHT_WU, 1e-4f);
    return true;
}

// 2. Head proportions: 1/8 rule (8-head canon)
DELVE_TEST(config_head_is_eighth_of_total) {
    float total = Config::ACTOR_HEIGHT_WU;
    EXPECT_NEAR(Config::ACTOR_HEAD_HEIGHT_WU, total / 8.0f, total * 0.001f);
    EXPECT_NEAR(Config::ACTOR_HEAD_WIDTH_WU, Config::ACTOR_HEAD_HEIGHT_WU * 0.75f,
                Config::ACTOR_HEAD_HEIGHT_WU * 0.001f);
    return true;
}

// 3. Torso height = 3/8 total
DELVE_TEST(config_torso_is_three_eighths) {
    float total = Config::ACTOR_HEIGHT_WU;
    EXPECT_NEAR(Config::ACTOR_TORSO_HEIGHT_WU, total * 0.375f, total * 0.001f);
    return true;
}

// 4. Shoulder width = 3/8 total
DELVE_TEST(config_shoulder_width_is_three_eighths) {
    float total = Config::ACTOR_HEIGHT_WU;
    EXPECT_NEAR(Config::ACTOR_SHOULDER_WIDTH_WU, total * 0.375f, total * 0.001f);
    return true;
}

// 5. Hip width = 1/4 total
DELVE_TEST(config_hip_width_is_quarter) {
    float total = Config::ACTOR_HEIGHT_WU;
    EXPECT_NEAR(Config::ACTOR_HIP_WIDTH_WU, total * 0.25f, total * 0.001f);
    return true;
}

// 6. Legs: upper + lower = 1/2 total
DELVE_TEST(config_legs_are_half_total) {
    float total = Config::ACTOR_HEIGHT_WU;
    float legs  = Config::ACTOR_UPPER_LEG_WU + Config::ACTOR_LOWER_LEG_WU;
    EXPECT_NEAR(legs, total * 0.5f, total * 0.001f);
    return true;
}

// 7. Upper leg == lower leg (symmetric)
DELVE_TEST(config_upper_lower_leg_equal) {
    EXPECT_NEAR(Config::ACTOR_UPPER_LEG_WU, Config::ACTOR_LOWER_LEG_WU,
                Config::ACTOR_HEIGHT_WU * 0.001f);
    return true;
}

// 8. Arm total (upper + forearm) ≈ 5/16 total
DELVE_TEST(config_arm_total_proportion) {
    float total = Config::ACTOR_HEIGHT_WU;
    float arm   = Config::ACTOR_UPPER_ARM_WU + Config::ACTOR_FOREARM_WU;
    // 3/16 + 2/16 = 5/16
    float expected = total * (3.0f / 16.0f + 0.125f);
    EXPECT_NEAR(arm, expected, total * 0.001f);
    return true;
}

// 9. BASALT_LAYER_HEIGHT_WU = 0.5 ft * 4 WU/ft = 2.0
DELVE_TEST(config_basalt_layer_height_wu) {
    EXPECT_NEAR(Config::BASALT_LAYER_HEIGHT_WU, 2.0f, 1e-4f);
    return true;
}

// 10. WORLD_UNITS_PER_FOOT = HEX_SIZE / HEX_DIAMETER_FEET = 8.0 / 2.0 = 4.0
DELVE_TEST(config_world_units_per_foot) {
    EXPECT_NEAR(Config::WORLD_UNITS_PER_FOOT, 4.0f, 1e-4f);
    return true;
}

// ---------------------------------------------------------------------------
// make_rest_pose() from skeleton.cpp
// ---------------------------------------------------------------------------

// 11. Rest pose ROOT is at origin.
DELVE_TEST(rest_pose_root_at_origin) {
    SkeletonPose p = make_rest_pose();
    EXPECT_NEAR(p.joints[(int)Joint::ROOT].x, 0.0f, 1e-3f);
    EXPECT_NEAR(p.joints[(int)Joint::ROOT].y, 0.0f, 1e-3f);
    EXPECT_NEAR(p.joints[(int)Joint::ROOT].z, 0.0f, 1e-3f);
    return true;
}

// 12. Legs descend in -z: L_ANKLE.z < L_KNEE.z < L_HIP.z (ROOT.z = 0)
DELVE_TEST(rest_pose_legs_descend) {
    SkeletonPose p = make_rest_pose();
    EXPECT_LT(p.joints[(int)Joint::L_ANKLE].z, p.joints[(int)Joint::L_KNEE].z);
    EXPECT_LT(p.joints[(int)Joint::L_KNEE].z, p.joints[(int)Joint::L_HIP].z);
    EXPECT_LT(p.joints[(int)Joint::R_ANKLE].z, p.joints[(int)Joint::R_KNEE].z);
    EXPECT_LT(p.joints[(int)Joint::R_KNEE].z, p.joints[(int)Joint::R_HIP].z);
    return true;
}

// 13. Torso rises in +z: CHEST.z > SPINE.z > ROOT.z
DELVE_TEST(rest_pose_torso_rises) {
    SkeletonPose p = make_rest_pose();
    EXPECT_GT(p.joints[(int)Joint::CHEST].z, p.joints[(int)Joint::SPINE].z);
    EXPECT_GT(p.joints[(int)Joint::SPINE].z, p.joints[(int)Joint::ROOT].z);
    EXPECT_GT(p.joints[(int)Joint::NECK].z, p.joints[(int)Joint::CHEST].z);
    EXPECT_GT(p.joints[(int)Joint::HEAD].z, p.joints[(int)Joint::NECK].z);
    return true;
}

// 14. Left/right symmetry: mirrored on X axis.
DELVE_TEST(rest_pose_bilateral_symmetry) {
    SkeletonPose p = make_rest_pose();
    float tol = 1e-3f;
    // Shoulders
    EXPECT_NEAR(p.joints[(int)Joint::L_SHOULDER].x, -p.joints[(int)Joint::R_SHOULDER].x, tol);
    EXPECT_NEAR(p.joints[(int)Joint::L_SHOULDER].z,  p.joints[(int)Joint::R_SHOULDER].z, tol);
    // Elbows
    EXPECT_NEAR(p.joints[(int)Joint::L_ELBOW].x, -p.joints[(int)Joint::R_ELBOW].x, tol);
    // Hips
    EXPECT_NEAR(p.joints[(int)Joint::L_HIP].x, -p.joints[(int)Joint::R_HIP].x, tol);
    EXPECT_NEAR(p.joints[(int)Joint::L_ANKLE].z, p.joints[(int)Joint::R_ANKLE].z, tol);
    return true;
}

// 15. Upper leg length ≈ Config::ACTOR_UPPER_LEG_WU
DELVE_TEST(rest_pose_leg_segment_lengths) {
    SkeletonPose p = make_rest_pose();
    float tol = Config::ACTOR_HEIGHT_WU * 0.01f;

    float l_upper = glm::length(p.joints[(int)Joint::L_KNEE]  - p.joints[(int)Joint::L_HIP]);
    float l_lower = glm::length(p.joints[(int)Joint::L_ANKLE] - p.joints[(int)Joint::L_KNEE]);
    EXPECT_NEAR(l_upper, Config::ACTOR_UPPER_LEG_WU, tol);
    EXPECT_NEAR(l_lower, Config::ACTOR_LOWER_LEG_WU, tol);
    return true;
}

// 16. Upper arm length ≈ Config::ACTOR_UPPER_ARM_WU
DELVE_TEST(rest_pose_arm_segment_lengths) {
    SkeletonPose p = make_rest_pose();
    float tol = Config::ACTOR_HEIGHT_WU * 0.01f;

    float upper_arm = glm::length(p.joints[(int)Joint::L_ELBOW]  - p.joints[(int)Joint::L_SHOULDER]);
    float forearm   = glm::length(p.joints[(int)Joint::L_WRIST]  - p.joints[(int)Joint::L_ELBOW]);
    EXPECT_NEAR(upper_arm, Config::ACTOR_UPPER_ARM_WU, tol);
    EXPECT_NEAR(forearm,   Config::ACTOR_FOREARM_WU,   tol);
    return true;
}

// 17. Total rest-pose vertical span (HEAD.z - L_ANKLE.z) ≈ ACTOR_TOTAL_HEIGHT_WU (±5%)
DELVE_TEST(rest_pose_total_height) {
    SkeletonPose p = make_rest_pose();
    float span = p.joints[(int)Joint::HEAD].z - p.joints[(int)Joint::L_ANKLE].z;
    float expected = Config::ACTOR_TOTAL_HEIGHT_WU;
    EXPECT_RANGE(span, expected * 0.80f, expected * 1.10f);
    return true;
}

// 18. ACTOR_NECK_HEIGHT_WU = ACTOR_HEIGHT_WU / 32
DELVE_TEST(config_neck_height_wu) {
    float expected = Config::ACTOR_HEIGHT_WU / 32.0f;
    EXPECT_NEAR(Config::ACTOR_NECK_HEIGHT_WU, expected, 1e-4f);
    return true;
}

// 19. ACTOR_WAIST_WIDTH_WU = 5/16 * total
DELVE_TEST(config_waist_width_wu) {
    float expected = Config::ACTOR_HEIGHT_WU * (5.0f / 16.0f);
    EXPECT_NEAR(Config::ACTOR_WAIST_WIDTH_WU, expected, Config::ACTOR_HEIGHT_WU * 0.001f);
    return true;
}

// 20. ACTOR_FOOT_LENGTH_WU = total / 7, legacy alias matches
DELVE_TEST(config_foot_length_wu) {
    float expected = Config::ACTOR_HEIGHT_WU / 7.0f;
    EXPECT_NEAR(Config::ACTOR_FOOT_LENGTH_WU, expected, 1e-4f);
    EXPECT_NEAR(Config::ACTOR_FOOT_LEN_WU, Config::ACTOR_FOOT_LENGTH_WU, 1e-6f);
    return true;
}

// 21. ACTOR_HAND_LENGTH_WU = total / 10, legacy alias matches
DELVE_TEST(config_hand_length_wu) {
    float expected = Config::ACTOR_HEIGHT_WU * 0.1f;
    EXPECT_NEAR(Config::ACTOR_HAND_LENGTH_WU, expected, 1e-4f);
    EXPECT_NEAR(Config::ACTOR_HAND_LEN_WU, Config::ACTOR_HAND_LENGTH_WU, 1e-6f);
    return true;
}

// 22. Legacy aliases ACTOR_HEIGHT_FT and ACTOR_TOTAL_HEIGHT_WU
DELVE_TEST(config_legacy_aliases) {
    EXPECT_NEAR(Config::ACTOR_HEIGHT_FT, Config::ACTOR_HEIGHT_FEET, 1e-6f);
    EXPECT_NEAR(Config::ACTOR_TOTAL_HEIGHT_WU, Config::ACTOR_HEIGHT_WU, 1e-6f);
    return true;
}

// 23. Waist is narrower than shoulder (5/16 < 3/8), wider than hip (5/16 > 1/4)
DELVE_TEST(config_waist_between_hip_and_shoulder) {
    EXPECT_LT(Config::ACTOR_WAIST_WIDTH_WU, Config::ACTOR_SHOULDER_WIDTH_WU);
    EXPECT_GT(Config::ACTOR_WAIST_WIDTH_WU, Config::ACTOR_HIP_WIDTH_WU);
    return true;
}

// 24. Forearm < upper arm (anatomically correct taper)
DELVE_TEST(config_forearm_shorter_than_upper_arm) {
    EXPECT_LT(Config::ACTOR_FOREARM_WU, Config::ACTOR_UPPER_ARM_WU);
    return true;
}
