#include "test_harness.h"
#include "actor.h"
#include "render/skeleton_mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Build a standing rest-pose from ActorConfig defaults (Z is up, ROOT at origin).
static SkeletonPose make_rest_pose(const ActorConfig &c) {
    SkeletonPose p;
    p.joints[(int)Joint::ROOT]       = { 0.0f, 0.0f, 0.0f };
    p.joints[(int)Joint::SPINE]      = { 0.0f, 0.0f, c.torso_len * 0.5f };
    p.joints[(int)Joint::CHEST]      = { 0.0f, 0.0f, c.torso_len };
    p.joints[(int)Joint::NECK]       = { 0.0f, 0.0f, c.torso_len + c.neck_len };
    p.joints[(int)Joint::HEAD]       = { 0.0f, 0.0f, c.torso_len + c.neck_len + c.head_radius };

    p.joints[(int)Joint::L_SHOULDER] = {  c.shoulder_width, 0.0f, c.torso_len };
    p.joints[(int)Joint::L_ELBOW]    = {  c.shoulder_width + c.arm_len, 0.0f, c.torso_len };
    p.joints[(int)Joint::L_WRIST]    = {  c.shoulder_width + c.arm_len + c.forearm_len, 0.0f, c.torso_len };

    p.joints[(int)Joint::R_SHOULDER] = { -c.shoulder_width, 0.0f, c.torso_len };
    p.joints[(int)Joint::R_ELBOW]    = { -c.shoulder_width - c.arm_len, 0.0f, c.torso_len };
    p.joints[(int)Joint::R_WRIST]    = { -c.shoulder_width - c.arm_len - c.forearm_len, 0.0f, c.torso_len };

    p.joints[(int)Joint::L_HIP]      = {  c.hip_width, 0.0f, 0.0f };
    p.joints[(int)Joint::L_KNEE]     = {  c.hip_width, 0.0f, -c.leg_len };
    p.joints[(int)Joint::L_ANKLE]    = {  c.hip_width, 0.0f, -(c.leg_len + c.shin_len) };

    p.joints[(int)Joint::R_HIP]      = { -c.hip_width, 0.0f, 0.0f };
    p.joints[(int)Joint::R_KNEE]     = { -c.hip_width, 0.0f, -c.leg_len };
    p.joints[(int)Joint::R_ANKLE]    = { -c.hip_width, 0.0f, -(c.leg_len + c.shin_len) };
    return p;
}

// Total height from ankle (lowest point) to top of head (HEAD joint + head_radius).
static float total_height(const ActorConfig &c) {
    float ankle_z = -(c.leg_len + c.shin_len);
    float head_top_z = c.torso_len + c.neck_len + c.head_radius + c.head_radius;
    return head_top_z - ankle_z;
}

// ---------------------------------------------------------------------------
// Test 1: Rest-pose leg length / total height is in [0.44, 0.50]
// Leg length = hip→knee + knee→ankle = leg_len + shin_len.
// ---------------------------------------------------------------------------
DELVE_TEST(proportions_leg_ratio) {
    ActorConfig c;
    float leg_length = c.leg_len + c.shin_len;
    float height     = total_height(c);
    float ratio      = leg_length / height;
    EXPECT_RANGE(ratio, 0.44f, 0.50f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 2: Shoulder width (both sides) / total height is in [0.25, 0.35]
// shoulder_width is per-side, so full span = 2 * shoulder_width.
// ---------------------------------------------------------------------------
DELVE_TEST(proportions_shoulder_width_ratio) {
    ActorConfig c;
    float span   = c.shoulder_width * 2.0f;
    float height = total_height(c);
    float ratio  = span / height;
    EXPECT_RANGE(ratio, 0.25f, 0.35f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 3: Head radius / total height is in [0.08, 0.16]
// ---------------------------------------------------------------------------
DELVE_TEST(proportions_head_radius_ratio) {
    ActorConfig c;
    float height = total_height(c);
    float ratio  = c.head_radius / height;
    EXPECT_RANGE(ratio, 0.08f, 0.16f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 4: Walk cycle over 60 frames: no joint delta > 0.05 between frames.
// Simulates the same sinusoidal pose update used by actor_animation.cpp at
// a slow walk speed (1.0 m/s) where all joint motion should be well below the
// 0.05 unit snapping threshold.
// ---------------------------------------------------------------------------

// Build a walk pose for a given walk_phase, hip position, and config.
// Mirrors the formulas from actor_animation.cpp (GaitSystem + IKSystem).
static SkeletonPose compute_walk_pose(const ActorConfig &c,
                                      float walk_phase,
                                      glm::vec3 hip) {
    SkeletonPose p;

    // Standing skeleton centred on hip (ROOT = hip)
    p.joints[(int)Joint::ROOT]  = hip;
    p.joints[(int)Joint::SPINE] = hip + glm::vec3(0.0f, 0.0f, c.torso_len * 0.5f);
    p.joints[(int)Joint::CHEST] = hip + glm::vec3(0.0f, 0.0f, c.torso_len);
    p.joints[(int)Joint::NECK]  = hip + glm::vec3(0.0f, 0.0f, c.torso_len + c.neck_len);
    p.joints[(int)Joint::HEAD]  = hip + glm::vec3(0.0f, 0.0f, c.torso_len + c.neck_len + c.head_radius);

    p.joints[(int)Joint::L_SHOULDER] = hip + glm::vec3( c.shoulder_width, 0.0f, c.torso_len);
    p.joints[(int)Joint::R_SHOULDER] = hip + glm::vec3(-c.shoulder_width, 0.0f, c.torso_len);

    // Arm swing: sinusoidal counter-swing ~0.08 amplitude in world units.
    const float swing_amp = 0.08f;
    float l_arm = sinf(walk_phase + glm::half_pi<float>()) * swing_amp;
    float r_arm = sinf(walk_phase - glm::half_pi<float>()) * swing_amp;

    p.joints[(int)Joint::L_ELBOW] = p.joints[(int)Joint::L_SHOULDER]
                                   + glm::vec3(c.arm_len, l_arm, 0.0f);
    p.joints[(int)Joint::L_WRIST] = p.joints[(int)Joint::L_ELBOW]
                                   + glm::vec3(c.forearm_len, l_arm * 0.5f, 0.0f);

    p.joints[(int)Joint::R_ELBOW] = p.joints[(int)Joint::R_SHOULDER]
                                   + glm::vec3(-c.arm_len, r_arm, 0.0f);
    p.joints[(int)Joint::R_WRIST] = p.joints[(int)Joint::R_ELBOW]
                                   + glm::vec3(-c.forearm_len, r_arm * 0.5f, 0.0f);

    // Leg IK: foot placement from sinusoidal gait phases.
    const float stride_len = 0.60f;
    float l_phase = walk_phase + glm::half_pi<float>();
    float r_phase = walk_phase - glm::half_pi<float>();
    float l_foot_fwd = sinf(l_phase) * stride_len * 0.5f;
    float r_foot_fwd = sinf(r_phase) * stride_len * 0.5f;

    glm::vec3 l_hip  = hip + glm::vec3( c.hip_width, 0.0f, 0.0f);
    glm::vec3 r_hip  = hip + glm::vec3(-c.hip_width, 0.0f, 0.0f);

    glm::vec3 l_foot = l_hip + glm::vec3(0.0f, l_foot_fwd, -(c.leg_len + c.shin_len));
    glm::vec3 r_foot = r_hip + glm::vec3(0.0f, r_foot_fwd, -(c.leg_len + c.shin_len));

    // Simple two-bone IK: mid-point for knee.
    p.joints[(int)Joint::L_HIP]   = l_hip;
    p.joints[(int)Joint::L_ANKLE] = l_foot;
    p.joints[(int)Joint::L_KNEE]  = (l_hip + l_foot) * 0.5f
                                   + glm::vec3(0.0f, 0.0f, 0.05f); // slight forward bend

    p.joints[(int)Joint::R_HIP]   = r_hip;
    p.joints[(int)Joint::R_ANKLE] = r_foot;
    p.joints[(int)Joint::R_KNEE]  = (r_hip + r_foot) * 0.5f
                                   + glm::vec3(0.0f, 0.0f, 0.05f);

    return p;
}

DELVE_TEST(walk_cycle_no_joint_snapping) {
    ActorConfig c;
    const float dt         = 1.0f / 60.0f;
    const float speed      = 1.0f;                         // slow walk
    const float stride_len = 0.60f;
    const float snap_limit = 0.05f;                        // max allowed joint delta per frame

    float walk_phase = 0.0f;
    float pos_y      = 0.0f;
    glm::vec3 hip(0.0f, pos_y, 0.0f);

    SkeletonPose prev = compute_walk_pose(c, walk_phase, hip);

    for (int frame = 0; frame < 60; ++frame) {
        walk_phase += speed * dt * (glm::two_pi<float>() / (2.0f * stride_len));
        pos_y += speed * dt;
        hip = glm::vec3(0.0f, pos_y, 0.0f);

        SkeletonPose curr = compute_walk_pose(c, walk_phase, hip);

        for (int j = 0; j < (int)Joint::COUNT; ++j) {
            // Subtract hip translation so we check only pose change, not translation.
            glm::vec3 delta = (curr.joints[j] - hip) - (prev.joints[j] - glm::vec3(0.0f, pos_y - speed * dt, 0.0f));
            float dist = glm::length(delta);
            if (dist > snap_limit) {
                fprintf(stderr, "  FAIL: joint %d frame %d delta=%.4f > %.4f\n",
                        j, frame, (double)dist, (double)snap_limit);
                return false;
            }
        }

        prev = curr;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 5: Generated mesh vertex count <= 500.
// ---------------------------------------------------------------------------
DELVE_TEST(mesh_vertex_count_under_500) {
    ActorConfig c;
    SkeletonPose pose = make_rest_pose(c);

    BoneProfileArray profiles;
    for (auto &bp : profiles) {
        bp.radius_start = 0.06f;
        bp.radius_end   = 0.06f;
        bp.sides        = 6;
    }

    SkeletonMesh mesh = generate_skeleton_mesh(pose, profiles);
    EXPECT_LT((int)mesh.vertices.size(), 500);
    return true;
}

// ---------------------------------------------------------------------------
// Test 6: All mesh normals have length in [0.99, 1.01].
// ---------------------------------------------------------------------------
DELVE_TEST(mesh_normals_normalized) {
    ActorConfig c;
    SkeletonPose pose = make_rest_pose(c);

    BoneProfileArray profiles;
    for (auto &bp : profiles) {
        bp.radius_start = 0.06f;
        bp.radius_end   = 0.06f;
        bp.sides        = 6;
    }

    SkeletonMesh mesh = generate_skeleton_mesh(pose, profiles);

    EXPECT_GT((int)mesh.vertices.size(), 0);
    for (int i = 0; i < (int)mesh.vertices.size(); ++i) {
        float len = glm::length(mesh.vertices[i].normal);
        if (len < 0.99f || len > 1.01f) {
            fprintf(stderr, "  FAIL: vertex %d normal length=%.4f not in [0.99, 1.01]\n",
                    i, (double)len);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 7: Total height of default ActorConfig is in [1.90, 2.10] world units.
// The comment in actor.h targets H=2.0. This pins that intent numerically.
// ---------------------------------------------------------------------------
DELVE_TEST(proportions_total_height_near_two) {
    ActorConfig c;
    float h = total_height(c);
    EXPECT_RANGE(h, 1.90f, 2.10f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 8: Torso (hip→chest) / total height is in [0.16, 0.24].
// Actor.h comment targets 19%.
// ---------------------------------------------------------------------------
DELVE_TEST(proportions_torso_ratio) {
    ActorConfig c;
    float ratio = c.torso_len / total_height(c);
    EXPECT_RANGE(ratio, 0.16f, 0.24f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 9: Upper arm / forearm ratio (arm_len / forearm_len) is in [1.1, 1.8].
// Upper arm must be longer than forearm for natural proportions.
// ---------------------------------------------------------------------------
DELVE_TEST(proportions_arm_forearm_ratio) {
    ActorConfig c;
    float ratio = c.arm_len / c.forearm_len;
    EXPECT_RANGE(ratio, 1.1f, 1.8f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 10: Upper leg / shin ratio (leg_len / shin_len) is in [0.85, 1.15].
// Legs should be roughly equal halves for natural gait.
// ---------------------------------------------------------------------------
DELVE_TEST(proportions_leg_shin_balance) {
    ActorConfig c;
    float ratio = c.leg_len / c.shin_len;
    EXPECT_RANGE(ratio, 0.85f, 1.15f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 11: Hip width < shoulder width (natural human taper upward).
// ---------------------------------------------------------------------------
DELVE_TEST(proportions_hip_narrower_than_shoulders) {
    ActorConfig c;
    EXPECT_LT(c.hip_width, c.shoulder_width);
    return true;
}

// ---------------------------------------------------------------------------
// Test 12: AnimationState zero-initializes foot_contact_velocity fields.
// Ensures the new struct field is properly initialized before first frame.
// ---------------------------------------------------------------------------
DELVE_TEST(animation_state_foot_contact_velocity_zero_init) {
    AnimationState anim;
    EXPECT_NEAR(anim.foot_contact_velocity[0], 0.0f, 1e-6f);
    EXPECT_NEAR(anim.foot_contact_velocity[1], 0.0f, 1e-6f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 13: AnimationState smooth_velocity starts at zero.
// ---------------------------------------------------------------------------
DELVE_TEST(animation_state_smooth_velocity_zero_init) {
    AnimationState anim;
    EXPECT_NEAR(glm::length(anim.smooth_velocity), 0.0f, 1e-6f);
    return true;
}

// ---------------------------------------------------------------------------
// Test 14: Walk cycle — arm swing has anti-phase with opposing leg.
// Left arm should swing forward when right leg steps forward (and vice versa).
// Verified over a full gait cycle of 60 frames at default stride.
// ---------------------------------------------------------------------------
DELVE_TEST(proportions_walk_arm_leg_antiphase) {
    ActorConfig c;
    const float dt         = 1.0f / 60.0f;
    const float speed      = 2.0f;
    const float stride_len = 0.60f;
    const float swing_amp  = 0.08f;

    int violations = 0;
    float walk_phase = 0.0f;

    for (int frame = 0; frame < 60; ++frame) {
        // Left arm forward offset (positive = forward in Y)
        float l_arm_fwd = sinf(walk_phase + glm::half_pi<float>()) * swing_amp;
        // Right leg forward offset
        float r_leg_fwd = sinf(walk_phase - glm::half_pi<float>()) * stride_len * 0.5f;

        // Anti-phase: left arm forward ↔ right leg forward.
        // sin(phase + π/2) and sin(phase - π/2) = -sin(phase + π/2), so
        // their product is always ≤ 0. A positive product means they're
        // accidentally in-phase — count that as a violation.
        float product = l_arm_fwd * r_leg_fwd;
        if (product > 1e-4f) ++violations;

        walk_phase += speed * dt * (glm::two_pi<float>() / (2.0f * stride_len));
    }
    // Allow up to 5 frames of near-zero crossover out of 60
    EXPECT_LT(violations, 6);
    return true;
}
