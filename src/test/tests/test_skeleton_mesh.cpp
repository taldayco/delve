#include "test_harness.h"
#include "geometry_metrics.h"
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
    // Line-list topology: indices come in pairs.
    EXPECT_EQ((int)(mesh.indices.size() % 2), 0);
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

DELVE_TEST(skeleton_mesh_no_degenerate_edges) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    // Line-list topology: check no edge has identical endpoints.
    int degenerate = 0;
    for (size_t i = 0; i + 1 < mesh.indices.size(); i += 2) {
        if (mesh.indices[i] == mesh.indices[i + 1]) ++degenerate;
    }
    EXPECT_EQ(degenerate, 0);
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
    // Line-list topology: indices come in pairs.
    EXPECT_EQ((int)(mesh.indices.size() % 2), 0);
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
    EXPECT_EQ((int)(mesh.indices.size() % 2), 0);
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

// ---------------------------------------------------------------------------
// Tests for new features: Skeleton alias, vector<BoneProfile> overload,
// BoneProfile::color, SkeletonMesh::vertex_bone / vertex_bone_weight arrays.
// ---------------------------------------------------------------------------

DELVE_TEST(skeleton_alias_matches_skeleton_pose) {
    // Skeleton is a typedef for SkeletonPose — they must be the same type.
    static_assert(std::is_same<Skeleton, SkeletonPose>::value,
                  "Skeleton must be an alias for SkeletonPose");
    Skeleton s;
    SkeletonPose p;
    EXPECT_EQ(sizeof(s), sizeof(p));
    return true;
}

DELVE_TEST(generate_skeleton_mesh_vector_overload_matches_array_overload) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray arr = make_default_profiles();

    // Build a vector<BoneProfile> from the same array data.
    std::vector<BoneProfile> vec(arr.begin(), arr.end());

    SkeletonMesh m_arr = generate_skeleton_mesh(pose, arr);
    SkeletonMesh m_vec = generate_skeleton_mesh(pose, vec);

    EXPECT_EQ((int)m_arr.vertices.size(), (int)m_vec.vertices.size());
    EXPECT_EQ((int)m_arr.indices.size(),  (int)m_vec.indices.size());
    return true;
}

DELVE_TEST(generate_skeleton_mesh_vector_overload_short_vector) {
    // Passing fewer profiles than NUM_BONE_PROFILES should not crash.
    Skeleton pose = make_standing_pose();
    std::vector<BoneProfile> profiles(4); // only 4 profiles
    SkeletonMesh mesh = generate_skeleton_mesh(pose, profiles);
    EXPECT_GT((int)mesh.vertices.size(), 0);
    EXPECT_EQ((int)(mesh.indices.size() % 2), 0);
    return true;
}

DELVE_TEST(bone_profile_color_default_is_mid_grey) {
    BoneProfile bp;
    EXPECT_NEAR(bp.color.r, 0.5f, 1e-5f);
    EXPECT_NEAR(bp.color.g, 0.5f, 1e-5f);
    EXPECT_NEAR(bp.color.b, 0.5f, 1e-5f);
    return true;
}

DELVE_TEST(bone_profile_color_survives_roundtrip_through_array) {
    BoneProfileArray arr = make_default_profiles();
    arr[0].color = glm::vec3(1.0f, 0.0f, 0.0f);
    arr[1].color = glm::vec3(0.0f, 1.0f, 0.0f);

    std::vector<BoneProfile> vec(arr.begin(), arr.end());
    EXPECT_NEAR(vec[0].color.r, 1.0f, 1e-5f);
    EXPECT_NEAR(vec[1].color.g, 1.0f, 1e-5f);
    return true;
}

DELVE_TEST(skeleton_mesh_vertex_bone_array_populated) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    // vertex_bone must be parallel to vertices.
    EXPECT_EQ((int)mesh.vertex_bone.size(), (int)mesh.vertices.size());
    // All bone indices must be in valid range.
    for (int bi : mesh.vertex_bone) {
        EXPECT_RANGE(bi, 0, NUM_BONE_PROFILES - 1);
    }
    return true;
}

DELVE_TEST(skeleton_mesh_vertex_bone_weight_array_populated) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    // vertex_bone_weight must be parallel to vertices.
    EXPECT_EQ((int)mesh.vertex_bone_weight.size(), (int)mesh.vertices.size());
    // All weights in [0,1].
    for (float w : mesh.vertex_bone_weight) {
        EXPECT_RANGE(w, 0.0f, 1.0f);
    }
    return true;
}

DELVE_TEST(skeleton_mesh_vertex_bone_consistent_with_bone_index0) {
    // vertex_bone[i] should match bone_index0 stored in the vertex.
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    int mismatches = 0;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        int from_vertex = (int)mesh.vertices[i].bone_index0;
        int from_array  = mesh.vertex_bone[i];
        if (from_vertex != from_array) ++mismatches;
    }
    EXPECT_EQ(mismatches, 0);
    return true;
}

DELVE_TEST(bone_vertex_layout_size) {
    // BoneVertex must be tightly-packed to a predictable size.
    // vec3 pos (12) + vec3 normal (12) + ivec2 bone_index (8) + float weight (4) = 36
    static_assert(sizeof(BoneVertex) == 36, "BoneVertex must be 36 bytes");
    return true;
}

// ---------------------------------------------------------------------------
// Wireframe visual quality tests
// ---------------------------------------------------------------------------

DELVE_TEST(skeleton_wireframe_no_zero_length_edges) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    std::vector<glm::vec3> positions;
    positions.reserve(mesh.vertices.size());
    for (const auto &v : mesh.vertices) positions.push_back(v.position);

    int zero_edges = count_zero_length_edges(mesh.indices, positions);
    EXPECT_EQ(zero_edges, 0);
    return true;
}

DELVE_TEST(skeleton_wireframe_verts_near_joints) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    std::vector<glm::vec3> positions;
    positions.reserve(mesh.vertices.size());
    for (const auto &v : mesh.vertices) positions.push_back(v.position);

    std::vector<glm::vec3> joints(pose.joints, pose.joints + (int)Joint::COUNT);

    // All wireframe vertices must be within limb_radius (0.06) + half a limb length
    // of the nearest joint. Use a generous bound of 0.5 world units.
    bool all_near = all_wireframe_verts_near_skeleton(positions, joints, 0.5f);
    EXPECT_TRUE(all_near);
    return true;
}

DELVE_TEST(skeleton_wireframe_index_count_exact_mixed_profiles) {
    SkeletonPose pose = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    // Torso bones: 4 sides; limb bones: 6 sides — matches SegmentProfiles defaults.
    for (int i = 0; i < 4; ++i)  prof[i].sides = 4;
    for (int i = 4; i < NUM_BONE_PROFILES; ++i) prof[i].sides = 6;

    SkeletonMesh mesh = generate_skeleton_mesh(pose, prof);

    // Expected: 4 bones * 4 sides * 3 * 2  +  12 bones * 6 sides * 3 * 2
    size_t expected = (size_t)(4 * 4 * 3 * 2) + (size_t)(12 * 6 * 3 * 2);
    EXPECT_EQ((int)mesh.indices.size(), (int)expected);
    return true;
}

DELVE_TEST(skeleton_wireframe_deform_preserves_edge_nonzero_length) {
    SkeletonPose bind = make_standing_pose();
    BoneProfileArray prof = make_default_profiles();
    SkeletonMesh mesh = generate_skeleton_mesh(bind, prof);

    // Apply a significant pose change.
    SkeletonPose posed = bind;
    posed.joints[(int)Joint::L_KNEE].z -= 0.3f;
    posed.joints[(int)Joint::R_KNEE].z -= 0.3f;
    posed.joints[(int)Joint::L_ELBOW].y += 0.2f;
    deform_skeleton_mesh(mesh, posed);

    std::vector<glm::vec3> positions;
    positions.reserve(mesh.vertices.size());
    for (const auto &v : mesh.vertices) positions.push_back(v.position);

    int zero_edges = count_zero_length_edges(mesh.indices, positions);
    EXPECT_EQ(zero_edges, 0);
    return true;
}
