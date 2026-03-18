#include "test_harness.h"
#include "core/gltf_loader.h"
#include "render/skeletal_animation.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

static GltfSkeleton make_nonidentity_skeleton() {
    GltfSkeleton skel{};
    skel.root_bone_index = 0;

    GltfBone root{};
    root.name         = "root";
    root.parent_index = -1;
    glm::mat4 bind_global = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 0.f, 0.f));
    root.inverse_bind_matrix   = glm::inverse(bind_global);
    root.local_rest_transform  = bind_global;
    skel.bones.push_back(root);

    GltfBone child{};
    child.name         = "child";
    child.parent_index = 0;
    glm::mat4 child_bind_global = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 5.f, 0.f));
    child.inverse_bind_matrix   = glm::inverse(child_bind_global);
    child.local_rest_transform  = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 5.f, 0.f));
    skel.bones.push_back(child);

    return skel;
}

DELVE_TEST(skinned_bone_palette_nonidentity) {
    GltfSkeleton skel = make_nonidentity_skeleton();

    const glm::mat4 &ibm = skel.bones[0].inverse_bind_matrix;
    EXPECT_NEAR(ibm[3][0], -10.f, 1e-4f);
    EXPECT_NEAR(ibm[3][1],   0.f, 1e-4f);
    EXPECT_NEAR(ibm[3][2],   0.f, 1e-4f);

    const glm::mat4 &ibm_child = skel.bones[1].inverse_bind_matrix;
    EXPECT_NEAR(ibm_child[3][0], -10.f, 1e-4f);
    EXPECT_NEAR(ibm_child[3][1],  -5.f, 1e-4f);
    EXPECT_NEAR(ibm_child[3][2],   0.f, 1e-4f);

    std::vector<BoneLocalTransform> locals(2);
    locals[0].translation = glm::vec3(10.f, 0.f, 0.f);
    locals[1].translation = glm::vec3(0.f,  5.f, 0.f);

    BonePalette palette = compute_bone_palette(skel, locals);

    for (int bone = 0; bone < 2; ++bone) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) {
                float expected = (c == r) ? 1.f : 0.f;
                EXPECT_NEAR(palette.bones[bone][c][r], expected, 1e-4f);
            }
    }
    return true;
}

DELVE_TEST(skinned_bone_palette_after_update) {
    GltfSkeleton skel = make_nonidentity_skeleton();

    GltfAnimationClip clip{};
    clip.name     = "test_move";
    clip.duration = 1.f;

    GltfAnimChannel ch{};
    ch.bone_index    = 0;
    ch.path          = "translation";
    ch.times         = {0.f};
    ch.translations  = {glm::vec3(12.f, 0.f, 0.f)};
    clip.channels.push_back(ch);

    GltfAnimChannel ch2{};
    ch2.bone_index   = 1;
    ch2.path         = "translation";
    ch2.times        = {0.f};
    ch2.translations = {glm::vec3(0.f, 5.f, 0.f)};
    clip.channels.push_back(ch2);

    AnimationPlayer player;
    player.set_clip(&clip);
    player.update(0.f);

    std::vector<BoneLocalTransform> locals(2);
    player.sample(locals);

    BonePalette palette = compute_bone_palette(skel, locals);

    EXPECT_NEAR(palette.bones[0][3][0], 2.f, 1e-3f);
    EXPECT_NEAR(palette.bones[0][3][1], 0.f, 1e-3f);
    EXPECT_NEAR(palette.bones[0][3][2], 0.f, 1e-3f);

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
