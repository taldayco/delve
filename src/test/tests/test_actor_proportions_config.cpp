#include "test_harness.h"
#include "config.h"
#include <cmath>

// Test 1: Verify world-unit scale constants.
// One basalt hex = 2 ft wide, HEX_SIZE = 8.0 WU → WU_PER_FT = 4.0.
DELVE_TEST(test_hex_world_unit_scale) {
    EXPECT_NEAR(Config::WU_PER_FT, 4.0f, 1e-5f);
    float expected_total = Config::ACTOR_HEIGHT_FEET * Config::WU_PER_FT;
    EXPECT_NEAR(Config::ACTOR_TOTAL_HEIGHT_WU, expected_total, 0.01f);
    return true;
}

// Test 2: Head proportions.
// Head height = total / 8 (classic 8-heads figure).
// Head width = head_height * 0.75.
DELVE_TEST(test_actor_head_proportions) {
    float total = Config::ACTOR_TOTAL_HEIGHT_WU;
    float tol   = total * 0.02f; // 2% tolerance

    EXPECT_NEAR(Config::ACTOR_HEAD_HEIGHT_WU, total / 8.0f, tol);
    EXPECT_NEAR(Config::ACTOR_HEAD_WIDTH_WU, Config::ACTOR_HEAD_HEIGHT_WU * 0.75f,
                Config::ACTOR_HEAD_HEIGHT_WU * 0.02f);
    return true;
}

// Test 3: Limb proportions.
// Upper leg == lower leg (1:1 symmetric legs).
// One-side arm reach from body centre (shoulder_half + upper_arm + forearm) ≈ total/2
// (Da Vinci's rule: full arm span ≈ total height).
DELVE_TEST(test_actor_limb_proportions) {
    float total = Config::ACTOR_TOTAL_HEIGHT_WU;
    float tol   = total * 0.02f;

    EXPECT_NEAR(Config::ACTOR_UPPER_LEG_WU, Config::ACTOR_LOWER_LEG_WU, tol);

    float arm_reach = Config::ACTOR_SHOULDER_WIDTH_WU * 0.5f
                    + Config::ACTOR_UPPER_ARM_WU
                    + Config::ACTOR_FOREARM_WU;
    EXPECT_NEAR(arm_reach, total / 2.0f, tol);
    return true;
}

// Test 4: Width proportions.
// Shoulder span / head width ≈ 4.0 (±0.1) — broad-shoulder rule.
// Hip width ≈ total / 4 (±2%).
DELVE_TEST(test_actor_width_proportions) {
    float total = Config::ACTOR_TOTAL_HEIGHT_WU;
    float tol   = total * 0.02f;

    float sw_ratio = Config::ACTOR_SHOULDER_WIDTH_WU / Config::ACTOR_HEAD_WIDTH_WU;
    EXPECT_NEAR(sw_ratio, 4.0f, 0.1f);

    EXPECT_NEAR(Config::ACTOR_HIP_WIDTH_WU, total / 4.0f, tol);
    return true;
}
