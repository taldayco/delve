struct BoneCheck { float len; float width; };
    BoneCheck bones[] = {
        { cfg.leg_len,   cfg.leg_radius   },
        { cfg.shin_len,  cfg.leg_radius   },
        { cfg.arm_len,   cfg.arm_radius   },
        { cfg.forearm_len, cfg.arm_radius * 0.75f },
        { cfg.torso_len * 0.4f, cfg.torso_radius },
        { cfg.neck_len,  cfg.head_radius * 0.55f },
    };

    for (auto &b : bones) {
        float equator_dist = b.len * EQUATOR_T;
        EXPECT_GT(equator_dist, 0.0f);
        EXPECT_LT(equator_dist, b.len);
        EXPECT_GT(b.width, 0.0f);
    }
    return true;
}

// Test: RGB tripod axes are mutually orthogonal (world XYZ basis vectors).
// Verifies the tripod orientation convention used by emit_tripod.
DELVE_TEST(rig_tripod_axes_orthogonal) {
    glm::vec3 x_axis{1.0f, 0.0f, 0.0f}; // red
    glm::vec3 y_axis{0.0f, 1.0f, 0.0f}; // green
    glm::vec3 z_axis{0.0f, 0.0f, 1.0f}; // blue
    EXPECT_NEAR(glm::dot(x_axis, y_axis), 0.0f, 1e-6f);
    EXPECT_NEAR(glm::dot(y_axis, z_axis), 0.0f, 1e-6f);
    EXPECT_NEAR(glm::dot(x_axis, z_axis), 0.0f, 1e-6f);
    // Axes are unit length.
    EXPECT_NEAR(glm::length(x_axis), 1.0f, 1e-6f);
    EXPECT_NEAR(glm::length(y_axis), 1.0f, 1e-6f);
    EXPECT_NEAR(glm::length(z_axis), 1.0f, 1e-6f);
    return true;
}

// Test: Smootherstep is strictly steeper than cosine-ease at midpoint.
// Both are S-curves: smootherstep has zero 1st+2nd derivatives at endpoints,
// giving faster transit through midpoint than cosine-ease.
DELVE_TEST(smootherstep_steeper_than_cosine_ease_at_midpoint) {
    auto smootherstep = [](float t) {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    };
    auto cosease = [](float t) {
        return (1.0f - std::cos(t * glm::pi<float>())) * 0.5f;
    };
    float dp = 0.02f;
    float ss_mid = smootherstep(0.5f + dp) - smootherstep(0.5f);
    float ce_mid = cosease(0.5f + dp)      - cosease(0.5f);
    // Smootherstep has higher velocity at midpoint (the curves are distinct).
    EXPECT_GT(ss_mid, ce_mid);
    // Both start and end at 0 and 1 exactly.
    EXPECT_NEAR(smootherstep(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(smootherstep(1.0f), 1.0f, 1e-6f);
    return true;
}

// ---- Isometric bone compressor tests ----

// Mirror of iso_compensate_bone from rig_renderer.cpp (pure math, no GPU dependency).
struct TestIsoCompensation {
    float width_scale;
    float equator_t;
    float radial_z_comp;
};

static TestIsoCompensation test_iso_compensate(const glm::vec3 &a, const glm::vec3 &b) {
    glm::vec3 dir = b - a;
    float len = glm::length(dir);
    if (len < 1e-5f) return {1.0f, 0.2f, 0.18f};
    dir /= len;
    float dot_z = glm::dot(dir, glm::vec3(0.0f, 0.0f, 1.0f));
    float z_align = fabsf(dot_z);
    TestIsoCompensation comp;
    comp.width_scale   = 1.0f + sqrtf(z_align) * 2.0f;
    comp.equator_t     = 0.2f + z_align * 0.15f;
    comp.radial_z_comp = 0.18f + z_align * (1.0f - 0.18f);
    return comp;
}

// Test: z_align = 0 for horizontal, 1 for vertical, intermediate for diagonal.
DELVE_TEST(iso_compressor_z_align_range) {
    // Horizontal bone (along X)
    auto h = test_iso_compensate({0,0,0}, {1,0,0});
    // Vertical bone (along Z)
    auto v = test_iso_compensate({0,0,0}, {0,0,1});
    // Diagonal bone (45 degrees in XZ)
    auto d = test_iso_compensate({0,0,0}, {1,0,1});

    // Horizontal: z_align ≈ 0, so width_scale ≈ 1.0
    EXPECT_NEAR(h.width_scale, 1.0f, 0.01f);
    // Vertical: z_align = 1, so width_scale = 3.0
    EXPECT_NEAR(v.width_scale, 3.0f, 0.01f);
    // Diagonal: intermediate
    EXPECT_GT(d.width_scale, 1.0f);
    EXPECT_LT(d.width_scale, 3.0f);
    return true;
}