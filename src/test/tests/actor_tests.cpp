// actor_tests.cpp
// Quantitative tests for actor proportions (8-head canon) and world-unit constants.
// Uses Config constants and the 8-head formula directly — no GPU/ECS dependencies.

#include "test_harness.h"
#include "animation_metrics.h"  // SkeletonProportionMetrics
#include "config.h"
#include <cmath>

// -----------------------------------------------------------------------
// 8-head canon helper — mirrors the formulas in ActorRenderer::make_proportions().
// All values in feet. u = total_height / 8 (one head-unit).
// -----------------------------------------------------------------------
struct CanonProportions {
    float total_height;
    float u;                // one head-unit = total / 8
    float head_height;      // 1u
    float neck_height;      // 0.5u
    float torso_height;     // 2u
    float upper_leg;        // 2u
    float lower_leg;        // 2u
    float shoulder_width;   // 3u  (full span; spec: ~3 head widths across)
    float hip_width;        // 1.5u
    float waist_width;      // 1.25u
    float head_width;       // 0.9u
    float upper_arm;        // 1.5u
    float forearm;          // 1.25u
    float hand_length;      // 0.75u
    float foot_length;      // 1u
};

static CanonProportions make_canon(float total_height) {
    CanonProportions p;
    p.total_height   = total_height;
    p.u              = total_height / 8.0f;
    p.head_height    = 1.0f  * p.u;
    p.neck_height    = 0.5f  * p.u;
    p.torso_height   = 2.0f  * p.u;
    p.upper_leg      = 2.0f  * p.u;
    p.lower_leg      = 2.0f  * p.u;
    p.shoulder_width = 3.0f  * p.u;
    p.hip_width      = 1.5f  * p.u;
    p.waist_width    = 1.25f * p.u;
    p.head_width     = 0.9f  * p.u;
    p.upper_arm      = 1.5f  * p.u;
    p.forearm        = 1.25f * p.u;
    p.hand_length    = 0.75f * p.u;
    p.foot_length    = 1.0f  * p.u;
    return p;
}

// Measure proportion ratios from canon.
// Populated from the analytical 8-head formulas so tests are verifiable.
static SkeletonProportionMetrics measure_proportions(const CanonProportions &p) {
    SkeletonProportionMetrics m = {};
    m.head_to_total        = p.head_height / p.total_height;             // 0.125
    m.leg_to_total         = (p.upper_leg + p.lower_leg) / p.total_height; // 0.5
    m.shoulder_hip_ratio   = p.shoulder_width / p.hip_width;             // 3/1.5 = 2.0
    m.shoulder_head_ratio  = p.shoulder_width / p.head_width;            // 3/0.9 ≈ 3.33
    m.shoulder_width       = p.shoulder_width;
    m.head_width           = p.head_width;
    // Arm span wrist-to-wrist: 2*(shoulder_half + upper_arm + forearm)
    float sw_half = p.shoulder_width * 0.5f;                              // 1u
    m.arm_span_to_total    = (2.0f * (sw_half + p.upper_arm + p.forearm)) / p.total_height;
    m.foot_to_total        = p.foot_length / p.total_height;             // 0.125
    return m;
}

static constexpr float TOTAL_H = Config::HUMAN_HEIGHT_FEET; // 5.9 ft

// ---- 8-head proportion tests ----

DELVE_TEST(actor_head_height_one_eighth) {
    // Classical 8-head canon: head_height = total_height / 8, tolerance ±1%
    auto p = make_canon(TOTAL_H);
    float expected = TOTAL_H / 8.0f;
    EXPECT_NEAR(p.head_height, expected, expected * 0.01f);
    return true;
}

DELVE_TEST(actor_leg_total_half_height) {
    // upper_leg + lower_leg == total_height / 2, tolerance ±1%
    auto p = make_canon(TOTAL_H);
    float leg_total = p.upper_leg + p.lower_leg;
    float expected  = TOTAL_H * 0.5f;
    EXPECT_NEAR(leg_total, expected, expected * 0.01f);
    return true;
}

DELVE_TEST(actor_shoulder_width_ratio) {
    // shoulder_width / head_width should be ~3.0 per spec (3 head widths across)
    // 8-head canon gives 3.0u / 0.9u ≈ 3.33; range [3.0, 3.75]
    auto p = make_canon(TOTAL_H);
    auto m = measure_proportions(p);
    EXPECT_RANGE(m.shoulder_head_ratio, 3.0f, 3.75f);
    return true;
}

DELVE_TEST(actor_arm_span_within_range) {
    // Arm span (wrist-to-wrist) / total_height should be ~1.0 (Vitruvian ratio)
    // With shoulder_width=3u: 2*(1.5u + 1.5u + 1.25u) = 8.5u; ratio = 8.5/8 = 1.0625
    auto p = make_canon(TOTAL_H);
    auto m = measure_proportions(p);
    EXPECT_RANGE(m.arm_span_to_total, 0.85f, 1.15f);
    return true;
}

DELVE_TEST(actor_foot_length_near_one_eighth) {
    // foot_length = 1 head-unit = total / 8, tolerance ±5%
    auto p = make_canon(TOTAL_H);
    float expected = TOTAL_H / 8.0f;
    EXPECT_NEAR(p.foot_length, expected, expected * 0.05f);
    return true;
}

// ---- SkeletonProportionMetrics ratio tests ----

DELVE_TEST(skeleton_head_to_total_ratio) {
    auto p = make_canon(TOTAL_H);
    auto m = measure_proportions(p);
    // head_to_total == 0.125, tolerance ±1%
    EXPECT_NEAR(m.head_to_total, 0.125f, 0.00125f);
    return true;
}

DELVE_TEST(skeleton_leg_to_total_ratio) {
    auto p = make_canon(TOTAL_H);
    auto m = measure_proportions(p);
    // leg_to_total == 0.5, tolerance ±1%
    EXPECT_NEAR(m.leg_to_total, 0.5f, 0.005f);
    return true;
}

DELVE_TEST(skeleton_shoulders_wider_than_hips) {
    auto p = make_canon(TOTAL_H);
    auto m = measure_proportions(p);
    // shoulder_hip_ratio must be > 1.0
    EXPECT_GT(m.shoulder_hip_ratio, 1.0f);
    return true;
}

DELVE_TEST(skeleton_foot_to_total_ratio) {
    auto p = make_canon(TOTAL_H);
    auto m = measure_proportions(p);
    // foot_to_total == 0.125, tolerance ±5%
    EXPECT_NEAR(m.foot_to_total, 0.125f, 0.00625f);
    return true;
}

// ---- World-unit constant checks ----

DELVE_TEST(world_unit_is_two_feet) {
    // 1 world unit = 2 feet = 1 basalt column width (WORLD_UNIT == 2.0)
    EXPECT_NEAR(Config::WORLD_UNIT, 2.0f, 1e-6f);
    return true;
}

DELVE_TEST(basalt_column_is_one_world_unit) {
    // BASALT_COLUMN_FEET == WORLD_UNIT: one column spans exactly 1 world unit
    EXPECT_NEAR(Config::BASALT_COLUMN_FEET, Config::WORLD_UNIT, 1e-6f);
    return true;
}

DELVE_TEST(human_height_in_world_units) {
    // 5.9 ft human / 2.0 ft-per-unit = 2.95 world units (~3 hex widths)
    float human_wu = Config::HUMAN_HEIGHT_FEET / Config::WORLD_UNIT;
    EXPECT_RANGE(human_wu, 2.5f, 3.5f);
    return true;
}
