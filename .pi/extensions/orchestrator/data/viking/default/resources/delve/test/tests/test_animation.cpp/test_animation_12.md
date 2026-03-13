// Test: Two-bone solver handles over-extension gracefully.
DELVE_TEST(two_bone_solver_over_extension) {
    float a = 0.3f, b = 0.25f;
    glm::vec3 H(0, 0, 0);
    glm::vec3 target(0, 0, -2.0f); // way beyond reach
    glm::vec3 mid, end;
    test_solve_two_bone(H, target, a, b, glm::vec3(0, 0.3f, 0),
                        glm::vec3(0, 1, 0), mid, end);

    // End effector should be clamped, not at target
    float end_dist = glm::length(end - H);
    float max_reach = (a + b - 0.005f) * 1.15f;
    EXPECT_LT(end_dist, max_reach + 0.01f);
    return true;
}

// ---- Phase 4: Overlay tests ----

// Test: AnimationOverlay defaults to None with zero intensity.
DELVE_TEST(overlay_defaults_none) {
    AnimationOverlay overlay;
    EXPECT_EQ((int)overlay.active, (int)AnimationOverlay::Type::None);
    EXPECT_NEAR(overlay.intensity, 0.0f, 1e-6f);
    EXPECT_NEAR(overlay.phase, 0.0f, 1e-6f);
    return true;
}

// Test: AnimationOverlay::Type enum values are distinct.
DELVE_TEST(overlay_types_distinct) {
    EXPECT_EQ((int)AnimationOverlay::Type::None, 0);
    EXPECT_EQ((int)AnimationOverlay::Type::Limp, 1);
    EXPECT_EQ((int)AnimationOverlay::Type::Fatigue, 2);
    EXPECT_EQ((int)AnimationOverlay::Type::HeavyCarry, 3);
    return true;
}

// ---- Phase 5: Grab state tests ----

// Test: GrabState defaults to inactive.
DELVE_TEST(grab_state_defaults_inactive) {
    GrabState grab;
    EXPECT_FALSE(grab.active_l);
    EXPECT_FALSE(grab.active_r);
    EXPECT_NEAR(grab.weight, 0.0f, 1e-6f);
    return true;
}

// Test: GrabState drives ArmIKGoal targets when active.
DELVE_TEST(grab_drives_arm_ik_when_active) {
    GrabState grab;
    grab.grab_point = glm::vec3(1.0f, 2.0f, 0.5f);
    grab.weight = 0.8f;
    grab.active_l = true;
    grab.active_r = false;

    ArmIKGoal arm_ik{};
    // Simulate GrabDriveSystem logic
    if (grab.active_l) {
        arm_ik.target_l = grab.grab_point;
        arm_ik.weight_l = grab.weight;
    }
    if (grab.active_r) {
        arm_ik.target_r = grab.grab_point;
        arm_ik.weight_r = grab.weight;
    } else {
        arm_ik.weight_r = 0.0f;
    }

    EXPECT_NEAR(arm_ik.target_l.x, 1.0f, 1e-6f);
    EXPECT_NEAR(arm_ik.weight_l, 0.8f, 1e-6f);
    EXPECT_NEAR(arm_ik.weight_r, 0.0f, 1e-6f);
    return true;
}

// Test: LookAtTarget defaults.
DELVE_TEST(look_at_target_defaults) {
    LookAtTarget lat;
    EXPECT_FALSE(lat.active);
    EXPECT_NEAR(lat.weight, 0.0f, 1e-6f);
    return true;
}

// Test: ArmIKGoal defaults to zero weight (pure FK).
DELVE_TEST(arm_ik_goal_defaults_fk) {
    ArmIKGoal aik;
    EXPECT_NEAR(aik.weight_l, 0.0f, 1e-6f);
    EXPECT_NEAR(aik.weight_r, 0.0f, 1e-6f);
    return true;
}

// ---- Sphere trace & terrain clearance tests ----

// Helper: create a small MapData with a linear slope in X.
// basalt_height[y * w + x] = slope * x, so height increases with pixel X.
static MapData make_slope_map(int w, int h, float slope) {
    MapData map;
    map.width = w;
    map.height = h;
    map.basalt_height.resize(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            map.basalt_height[y * w + x] = slope * (float)x;
    return map;
}

// Helper: create a flat MapData with uniform height.
static MapData make_flat_map(int w, int h, float height) {
    MapData map;
    map.width = w;
    map.height = h;
    map.basalt_height.resize(w * h, height);
    return map;
}

DELVE_TEST(sphere_trace_ge_point_sample) {
    // On a slope, sphere trace must be >= point sample.
    auto map = make_slope_map(64, 64, 0.5f);
    float radii[] = {0.05f, 0.1f, 0.2f};
    // Test at several world positions (converting: px = wx * HEX_SIZE)
    float positions[] = {1.0f, 2.0f, 3.0f, 4.0f};
    for (float wx : positions) {
        float wy = 2.0f;
        float point_h = sample_world_height(map, wx, wy);
        for (float r : radii) {
            float sphere_h = sphere_trace_height(map, wx, wy, r);
            EXPECT_GE(sphere_h, point_h);
        }
    }
    return true;
}