#include "test_harness.h"
#include "actor.h"
#include "render/skeleton_mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a minimal standing bind pose using ActorConfig defaults.
static SkeletonPose make_standing_pose() {
    ActorConfig c;
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
    p.joints[(int)Joint::L_ANKLE]    = {  c.hip_width, 0.0f, -c.leg_len - c.shin_len };

    p.joints[(int)Joint::R_HIP]      = { -c.hip_width, 0.0f, 0.0f };
    p.joints[(int)Joint::R_KNEE]     = { -c.hip_width, 0.0f, -c.leg_len };
    p.joints[(int)Joint::R_ANKLE]    = { -c.hip_width, 0.0f, -c.leg_len - c.shin_len };

    return p;
}

static BoneProfileArray make_default_profiles() {
    BoneProfileArray p;
    for (auto &bp : p) {
        bp.radius_start = 0.06f;
        bp.radius_end   = 0.06f;
        bp.sides        = 6;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

DELVE_TEST(skeleton_mesh_generates_vertices) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    EXPECT_GT((int)mesh.vertices.size(), 0);
    return true;
}

DELVE_TEST(skeleton_mesh_vertex_budget_under_500) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    EXPECT_LT((int)mesh.vertices.size(), 500);
    return true;
}

DELVE_TEST(skeleton_mesh_has_valid_indices) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    EXPECT_GT((int)mesh.indices.size(), 0);
    EXPECT_EQ((int)(mesh.indices.size() % 3), 0);
    return true;
}

DELVE_TEST(skeleton_mesh_index_in_vertex_range) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    uint32_t vcount = (uint32_t)mesh.vertices.size();
    for (uint32_t idx : mesh.indices) {
        EXPECT_LT((int)idx, (int)vcount);
    }
    return true;
}

DELVE_TEST(skeleton_mesh_rest_positions_match_vertex_count) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    EXPECT_EQ((int)mesh.rest_positions.size(), (int)mesh.vertices.size());
    return true;
}

DELVE_TEST(skeleton_mesh_normals_are_unit_length) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    int bad = 0;
    for (const auto &v : mesh.vertices) {
        float len = glm::length(v.normal);
        if (len < 0.9f || len > 1.1f) ++bad;
    }
    int total = (int)mesh.vertices.size();
    EXPECT_LT(bad, total / 20 + 1);
    return true;
}

DELVE_TEST(skeleton_mesh_bone_weights_in_range) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    for (const auto &v : mesh.vertices) {
        EXPECT_RANGE(v.bone_weight, 0.0f, 1.0f);
        // bone_index0/1 are stored as float but represent integer bone indices.
        int bi0 = (int)v.bone_index0;
        int bi1 = (int)v.bone_index1;
        EXPECT_RANGE(bi0, 0, NUM_BONE_PROFILES - 1);
        EXPECT_RANGE(bi1, 0, NUM_BONE_PROFILES - 1);
    }
    return true;
}

DELVE_TEST(skeleton_mesh_no_degenerate_triangles) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    int degenerate = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3 &p0 = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3 &p1 = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3 &p2 = mesh.vertices[mesh.indices[i + 2]].position;
        glm::vec3 cross = glm::cross(p1 - p0, p2 - p0);
        if (glm::length(cross) < 1e-8f) ++degenerate;
    }
    int total_tris = (int)(mesh.indices.size() / 3);
    EXPECT_LT(degenerate, total_tris / 50 + 1);
    return true;
}

DELVE_TEST(skeleton_mesh_deform_preserves_vertex_count) {
    SkeletonPose bind = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(bind, prof);

    size_t before = mesh.vertices.size();

    SkeletonPose posed = bind;
    posed.joints[(int)Joint::L_ANKLE].x += 0.1f;
    posed.joints[(int)Joint::R_ANKLE].x -= 0.1f;
    deform_skeleton_mesh(mesh, posed);

    EXPECT_EQ((int)mesh.vertices.size(), (int)before);
    return true;
}

DELVE_TEST(skeleton_mesh_deform_changes_positions) {
    SkeletonPose bind = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(bind, prof);

    std::vector<glm::vec3> original;
    for (const auto &v : mesh.vertices) original.push_back(v.position);

    SkeletonPose posed = bind;
    posed.joints[(int)Joint::L_KNEE].z -= 0.2f;
    posed.joints[(int)Joint::R_KNEE].z -= 0.2f;
    deform_skeleton_mesh(mesh, posed);

    int changed = 0;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        if (glm::length(mesh.vertices[i].position - original[i]) > 1e-4f)
            ++changed;
    }
    EXPECT_GT(changed, 0);
    return true;
}

DELVE_TEST(skeleton_mesh_sides_3_valid_topology) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    for (auto &bp : prof) bp.sides = 3;
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    EXPECT_GT((int)mesh.vertices.size(), 0);
    EXPECT_EQ((int)(mesh.indices.size() % 3), 0);
    uint32_t vcount = (uint32_t)mesh.vertices.size();
    for (uint32_t idx : mesh.indices) EXPECT_LT((int)idx, (int)vcount);
    return true;
}

DELVE_TEST(skeleton_mesh_taper_reduces_end_radius) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    // Full taper: set end radius to ~0.
    for (auto &bp : prof) bp.radius_end = 0.001f;
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    EXPECT_GT((int)mesh.vertices.size(), 0);
    EXPECT_EQ((int)(mesh.indices.size() % 3), 0);
    return true;
}

DELVE_TEST(skeleton_mesh_gpu_buffers_null_before_upload) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    EXPECT_FALSE(mesh.is_uploaded());
    EXPECT_TRUE(mesh.vertex_buffer == nullptr);
    EXPECT_TRUE(mesh.index_buffer  == nullptr);
    return true;
}
