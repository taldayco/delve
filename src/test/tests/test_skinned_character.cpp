// test_skinned_character.cpp
// Tests for skinned character bone palette correctness:
//   1. skinned_bone_palette_nonidentity  — skeleton with non-identity inverse
//      bind matrices produces non-identity bone palette entries.
//   2. skinned_bone_palette_after_update — AnimationPlayer with a translation
//      channel, after update(0) + sample(), compute_bone_palette is non-identity.

#include "test_harness.h"
#include "core/gltf_loader.h"
#include "render/skeletal_animation.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

// Helper: build a simple two-bone skeleton where root is offset by (10,0,0)
// in bind pose, so inverse_bind_matrix is the inverse of T(10,0,0).
static GltfSkeleton make_nonidentity_skeleton() {
    GltfSkeleton skel{};
    skel.root_bone_index = 0;

    GltfBone root{};
    root.name         = "root";
    root.parent_index = -1;
    // Bind pose: root at (10,0,0)
    glm::mat4 bind_global = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 0.f, 0.f));
    root.inverse_bind_matrix   = glm::inverse(bind_global);
    root.local_rest_transform  = bind_global;
    skel.bones.push_back(root);

    GltfBone child{};
    child.name         = "child";
    child.parent_index = 0;
    // Child sits at (10,5,0) globally in bind pose
    glm::mat4 child_bind_global = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 5.f, 0.f));
    child.inverse_bind_matrix   = glm::inverse(child_bind_global);
    child.local_rest_transform  = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 5.f, 0.f));
    skel.bones.push_back(child);

    return skel;
}

// ---- Test 1: inverse_bind_matrices are non-identity ---------------------------

DELVE_TEST(skinned_bone_palette_nonidentity) {
    GltfSkeleton skel = make_nonidentity_skeleton();

    // Verify the inverse bind matrix for root is NOT identity
    // (root is offset, so its inverse should have a non-zero translation column)
    const glm::mat4 &ibm = skel.bones[0].inverse_bind_matrix;
    // Column 3 (translation) of the inverse of T(10,0,0) should be (-10,0,0,1)
    EXPECT_NEAR(ibm[3][0], -10.f, 1e-4f);
    EXPECT_NEAR(ibm[3][1],   0.f, 1e-4f);
    EXPECT_NEAR(ibm[3][2],   0.f, 1e-4f);

    // Verify child's inverse bind matrix is also non-identity
    const glm::mat4 &ibm_child = skel.bones[1].inverse_bind_matrix;
    EXPECT_NEAR(ibm_child[3][0], -10.f, 1e-4f);
    EXPECT_NEAR(ibm_child[3][1],  -5.f, 1e-4f);
    EXPECT_NEAR(ibm_child[3][2],   0.f, 1e-4f);

    // Now compute the bone palette in rest pose (local transforms = rest transforms).
    // In rest pose the skinning matrix should be identity (GlobalRest * InverseBind = I).
    std::vector<BoneLocalTransform> locals(2);
    locals[0].translation = glm::vec3(10.f, 0.f, 0.f); // root rest
    locals[1].translation = glm::vec3(0.f,  5.f, 0.f); // child rest (local)

    BonePalette palette = compute_bone_palette(skel, locals);

    // Both entries should be close to identity in rest pose
    for (int bone = 0; bone < 2; ++bone) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) {
                float expected = (c == r) ? 1.f : 0.f;
                EXPECT_NEAR(palette.bones[bone][c][r], expected, 1e-4f);
            }
    }
    return true;
}

// ---- Test 2: bone palette is non-identity after AnimationPlayer update --------

DELVE_TEST(skinned_bone_palette_after_update) {
    // Build skeleton with non-identity inverse bind matrices (root at (10,0,0))
    GltfSkeleton skel = make_nonidentity_skeleton();

    // Build an animation clip that translates root to (12,0,0) at t=0
    GltfAnimationClip clip{};
    clip.name     = "test_move";
    clip.duration = 1.f;

    GltfAnimChannel ch{};
    ch.bone_index    = 0;
    ch.path          = "translation";
    ch.times         = {0.f};
    ch.translations  = {glm::vec3(12.f, 0.f, 0.f)};
    clip.channels.push_back(ch);

    // Child stays at rest locally
    GltfAnimChannel ch2{};
    ch2.bone_index   = 1;
    ch2.path         = "translation";
    ch2.times        = {0.f};
    ch2.translations = {glm::vec3(0.f, 5.f, 0.f)};
    clip.channels.push_back(ch2);

    AnimationPlayer player;
    player.set_clip(&clip);
    player.update(0.f); // sample at t=0

    std::vector<BoneLocalTransform> locals(2);
    player.sample(locals);

    // Root moved from rest (10,0,0) → animated (12,0,0), delta = +2
    // BonePalette root = T(12,0,0) * inverse(T(10,0,0)) = T(2,0,0)
    BonePalette palette = compute_bone_palette(skel, locals);

    // Root entry: translation column should be (2,0,0)
    EXPECT_NEAR(palette.bones[0][3][0], 2.f, 1e-3f);
    EXPECT_NEAR(palette.bones[0][3][1], 0.f, 1e-3f);
    EXPECT_NEAR(palette.bones[0][3][2], 0.f, 1e-3f);

    // The root bone palette must be non-identity (at least one off-diagonal or
    // non-unity diagonal element deviates from identity)
    bool non_identity = false;
    for (int c = 0; c < 4 && !non_identity; ++c)
        for (int r = 0; r < 4 && !non_identity; ++r) {
            float expected = (c == r) ? 1.f : 0.f;
            if (std::abs(palette.bones[0][c][r] - expected) > 1e-3f)
                non_identity = true;
        }
    EXPECT_TRUE(non_identity);

    return true;
}
