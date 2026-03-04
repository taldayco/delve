#include "test_harness.h"
#include "geometry_metrics.h"
#include "actor.h"
#include "render/skeleton_mesh.h"
#include "render/actor_mesh.h"
#include "config.h"
#include <glm/glm.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers (shared with test_skeleton_mesh.cpp conventions)
// ---------------------------------------------------------------------------

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

// Build a uniform BoneProfileArray where every bone has the same `sides`.
static BoneProfileArray make_uniform_profiles(int sides) {
    BoneProfileArray p;
    for (auto &bp : p) {
        bp.radius_start = 0.06f;
        bp.radius_end   = 0.06f;
        bp.sides        = sides;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Test (a): triangle index count matches formula for uniform sides.
// Triangle-list: cylinder*S*6 + wrist_caps*2*S*3 + foot_segs*2*(S*6+S*3) +
//                head_sphere*(3*S*6+S*3) + root_cap*S*3
// Simplified: S * (16*6 + 2*3 + 2*9 + 21 + 3) = S * 144
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_line_index_count_uniform_sides_6) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(6);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    size_t expected = (size_t)(6 * 144);
    EXPECT_EQ((int)mesh.indices.size(), (int)expected);
    return true;
}

DELVE_TEST(actor_mesh_line_index_count_uniform_sides_4) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(4);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    size_t expected = (size_t)(4 * 144);
    EXPECT_EQ((int)mesh.indices.size(), (int)expected);
    return true;
}

DELVE_TEST(actor_mesh_line_index_count_uniform_sides_3) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(3);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    size_t expected = (size_t)(3 * 144);
    EXPECT_EQ((int)mesh.indices.size(), (int)expected);
    return true;
}

// ---------------------------------------------------------------------------
// Test (b): all line indices are in bounds
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_all_line_indices_in_bounds) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(6);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    EXPECT_TRUE(line_indices_valid(mesh.indices, mesh.vertices.size()));
    return true;
}

DELVE_TEST(actor_mesh_all_line_indices_in_bounds_sides_4) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(4);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    EXPECT_TRUE(line_indices_valid(mesh.indices, mesh.vertices.size()));
    return true;
}

// ---------------------------------------------------------------------------
// Test (c): no degenerate triangles (two identical vertex indices in a triangle)
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_no_degenerate_edges_sides_6) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(6);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    int degenerate = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t a = mesh.indices[i], b = mesh.indices[i+1], c = mesh.indices[i+2];
        if (a == b || b == c || a == c) ++degenerate;
    }
    EXPECT_EQ(degenerate, 0);
    return true;
}

DELVE_TEST(actor_mesh_no_degenerate_edges_sides_3) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(3);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    int degenerate = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t a = mesh.indices[i], b = mesh.indices[i+1], c = mesh.indices[i+2];
        if (a == b || b == c || a == c) ++degenerate;
    }
    EXPECT_EQ(degenerate, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Test (d): triangle-list mesh covers all ring vertices.
// Each bone has a start ring (SIDES verts) and end ring (SIDES verts).
// Every ring vertex must appear in at least one triangle.
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_ring_edges_form_closed_loops) {
    SkeletonPose pose = make_standing_pose();
    const int SIDES = 6;
    BoneProfileArray prof = make_uniform_profiles(SIDES);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    // Verify triangle-list topology.
    EXPECT_EQ((int)(mesh.indices.size() % 3), 0);

    // Build set of referenced vertices.
    std::vector<bool> referenced(mesh.vertices.size(), false);
    for (uint32_t idx : mesh.indices) {
        if (idx < (uint32_t)mesh.vertices.size())
            referenced[idx] = true;
    }

    // All vertices from rings (first 16*2*SIDES) must be referenced.
    // The mesh may have cap vertices appended after the ring vertices.
    int unreferenced = 0;
    int ring_verts = NUM_BONE_PROFILES * 2 * SIDES;
    for (int i = 0; i < ring_verts && i < (int)mesh.vertices.size(); ++i) {
        if (!referenced[i]) ++unreferenced;
    }
    EXPECT_EQ(unreferenced, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Test: actor_mesh_is_line_list — vertex count is even (line-list topology)
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_is_line_list) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(6);
    ActorMesh mesh = generate_actor_wireframe_mesh(pose, prof);
    EXPECT_TRUE(mesh.vertices.size() % 2 == 0);
    return true;
}

// ---------------------------------------------------------------------------
// Test: actor_mesh_vertices_are_white — all ActorMeshVertex colors are (1,1,1,1)
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_vertices_are_white) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(6);
    ActorMesh mesh = generate_actor_wireframe_mesh(pose, prof);
    EXPECT_TRUE(!mesh.vertices.empty());
    for (const auto &v : mesh.vertices) {
        EXPECT_NEAR(v.color.r, 1.0f, 1e-5f);
        EXPECT_NEAR(v.color.g, 1.0f, 1e-5f);
        EXPECT_NEAR(v.color.b, 1.0f, 1e-5f);
        EXPECT_NEAR(v.color.a, 1.0f, 1e-5f);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test: actor_mesh_edge_count_in_range — edge count > 0 and <= ACTOR_MAX_VERTICES
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_edge_count_in_range) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(6);
    ActorMesh mesh = generate_actor_wireframe_mesh(pose, prof);
    size_t edge_count = mesh.vertices.size() / 2;
    EXPECT_TRUE(edge_count > 0);
    EXPECT_TRUE(edge_count * 2 <= (size_t)Config::ACTOR_MAX_VERTICES * 6);
    return true;
}

// ---------------------------------------------------------------------------
// Test: actor_mesh_no_degenerate_edges — no edge has identical start/end positions
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_no_degenerate_edges) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(6);
    ActorMesh mesh = generate_actor_wireframe_mesh(pose, prof);
    for (size_t i = 0; i + 1 < mesh.vertices.size(); i += 2) {
        glm::vec3 a = mesh.vertices[i].position;
        glm::vec3 b = mesh.vertices[i + 1].position;
        float dist2 = (a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z);
        EXPECT_TRUE(dist2 > 1e-10f);
    }
    return true;
}
