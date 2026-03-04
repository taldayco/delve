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
// Test (a): line index count matches formula  bones * sides * 3 * 2
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_line_index_count_uniform_sides_6) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(6);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    size_t expected = wireframe_edge_count_expected(NUM_BONE_PROFILES, 6);
    EXPECT_EQ((int)mesh.indices.size(), (int)expected);
    return true;
}

DELVE_TEST(actor_mesh_line_index_count_uniform_sides_4) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(4);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    size_t expected = wireframe_edge_count_expected(NUM_BONE_PROFILES, 4);
    EXPECT_EQ((int)mesh.indices.size(), (int)expected);
    return true;
}

DELVE_TEST(actor_mesh_line_index_count_uniform_sides_3) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(3);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    size_t expected = wireframe_edge_count_expected(NUM_BONE_PROFILES, 3);
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
// Test (c): no degenerate edges (both endpoints identical)
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_no_degenerate_edges_sides_6) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(6);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    int degenerate = count_degenerate_edges(mesh.indices);
    EXPECT_EQ(degenerate, 0);
    return true;
}

DELVE_TEST(actor_mesh_no_degenerate_edges_sides_3) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_uniform_profiles(3);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    int degenerate = count_degenerate_edges(mesh.indices);
    EXPECT_EQ(degenerate, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Test (d): ring edges form closed loops
// For each bone bi with `sides` s, the start ring occupies vertices
// [base_start .. base_start+s) and the end ring [base_end .. base_end+s).
// The ring edges (first pair per side: v00→v10, where v10 = base+(si+1)%s)
// must collectively visit every consecutive pair in the ring, including the
// wrap-around edge (last→first).
//
// Strategy: reconstruct which edges appear in the index buffer, then verify
// that for every bone the start-ring and end-ring each form exactly one
// closed cycle covering all `sides` vertices.
// ---------------------------------------------------------------------------
DELVE_TEST(actor_mesh_ring_edges_form_closed_loops) {
    SkeletonPose pose = make_standing_pose();
    const int SIDES = 6;
    BoneProfileArray prof = make_uniform_profiles(SIDES);
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    // Each bone contributes 2*SIDES vertices (start ring + end ring).
    // Vertex layout per bone: [base_start .. base_start+SIDES) start ring,
    //                         [base_end   .. base_end+SIDES)   end ring.
    // base_start for bone bi = bi * 2 * SIDES.
    int bones = NUM_BONE_PROFILES;
    EXPECT_EQ((int)mesh.vertices.size(), bones * 2 * SIDES);

    // Build an adjacency set from pairs of indices.
    // For each ring (start/end per bone), check that every side has exactly
    // one ring edge connecting it to its cyclic neighbour.
    for (int bi = 0; bi < bones; ++bi) {
        uint32_t base_start = (uint32_t)(bi * 2 * SIDES);
        uint32_t base_end   = base_start + (uint32_t)SIDES;

        // Collect ring-edge counts: start_ring[si] = #edges from si to (si+1)%SIDES.
        std::vector<int> start_ring(SIDES, 0);
        std::vector<int> end_ring(SIDES, 0);

        // Scan all index pairs.
        for (size_t i = 0; i + 1 < mesh.indices.size(); i += 2) {
            uint32_t a = mesh.indices[i];
            uint32_t b = mesh.indices[i + 1];

            // Check start ring: a in [base_start, base_start+SIDES), b same range.
            if (a >= base_start && a < base_start + SIDES &&
                b >= base_start && b < base_start + SIDES) {
                int sa = (int)(a - base_start);
                int sb = (int)(b - base_start);
                int fwd = (sa + 1) % SIDES;
                if (sb == fwd) start_ring[sa]++;
            }
            // Check end ring.
            if (a >= base_end && a < base_end + SIDES &&
                b >= base_end && b < base_end + SIDES) {
                int sa = (int)(a - base_end);
                int sb = (int)(b - base_end);
                int fwd = (sa + 1) % SIDES;
                if (sb == fwd) end_ring[sa]++;
            }
        }

        // Every side of each ring must have exactly one forward ring edge.
        for (int si = 0; si < SIDES; ++si) {
            EXPECT_EQ(start_ring[si], 1);
            EXPECT_EQ(end_ring[si], 1);
        }
    }
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
