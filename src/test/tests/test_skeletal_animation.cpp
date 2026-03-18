#include "test_harness.h"
#include "core/gltf_loader.h"
#include "render/skeletal_animation.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstring>

DELVE_TEST(skinned_vertex_size_68_bytes) {
    EXPECT_EQ((int)sizeof(SkinnedVertex), 68);
    return true;
}

DELVE_TEST(skinned_vertex_field_offsets) {
    SkinnedVertex v{};
    const uint8_t *base = reinterpret_cast<const uint8_t *>(&v);
    EXPECT_EQ((int)(reinterpret_cast<const uint8_t *>(&v.position)  - base), 0);
    EXPECT_EQ((int)(reinterpret_cast<const uint8_t *>(&v.normal)    - base), 12);
    EXPECT_EQ((int)(reinterpret_cast<const uint8_t *>(&v.texcoord)  - base), 24);
    EXPECT_EQ((int)(reinterpret_cast<const uint8_t *>(&v.tangent)   - base), 32);
    EXPECT_EQ((int)(reinterpret_cast<const uint8_t *>(&v.joints)    - base), 48);
    EXPECT_EQ((int)(reinterpret_cast<const uint8_t *>(&v.weights)   - base), 52);
    return true;
}

DELVE_TEST(gltf_bone_root_parent_minus_one) {
    GltfBone bone{};
    bone.parent_index = -1;
    bone.name = "root";
    bone.inverse_bind_matrix = glm::mat4(1.0f);
    bone.local_rest_transform = glm::mat4(1.0f);
    EXPECT_EQ(bone.parent_index, -1);
    return true;
}

DELVE_TEST(gltf_skeleton_default_root_index_zero) {
    GltfSkeleton skel{};
    EXPECT_EQ(skel.root_bone_index, 0);
    return true;
}

DELVE_TEST(gltf_anim_channel_path_strings) {
    GltfAnimChannel ch{};
    ch.path = "translation";
    EXPECT_TRUE(ch.path == "translation");
    ch.path = "rotation";
    EXPECT_TRUE(ch.path == "rotation");
    ch.path = "scale";
    EXPECT_TRUE(ch.path == "scale");
    return true;
}

DELVE_TEST(bone_palette_capacity_65) {
    BonePalette palette{};
    EXPECT_GE((int)(sizeof(palette.bones) / sizeof(palette.bones[0])), 65);
    return true;
}

DELVE_TEST(bone_palette_default_identity) {
    BonePalette palette{};
    for (int i = 0; i < 65; ++i)
        palette.bones[i] = glm::mat4(1.0f);
    for (int i = 0; i < 65; ++i) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) {
                float expected = (c == r) ? 1.0f : 0.0f;
                EXPECT_NEAR(palette.bones[i][c][r], expected, 1e-6f);
            }
    }
    return true;
}

DELVE_TEST(bone_local_transform_default_values) {
    BoneLocalTransform t{};
    EXPECT_NEAR(t.translation.x, 0.0f, 1e-6f);
    EXPECT_NEAR(t.translation.y, 0.0f, 1e-6f);
    EXPECT_NEAR(t.translation.z, 0.0f, 1e-6f);
    EXPECT_NEAR(t.scale.x, 1.0f, 1e-4f);
    EXPECT_NEAR(t.scale.y, 1.0f, 1e-4f);
    EXPECT_NEAR(t.scale.z, 1.0f, 1e-4f);
    EXPECT_NEAR(t.rotation.w, 1.0f, 1e-4f);
    EXPECT_NEAR(t.rotation.x, 0.0f, 1e-4f);
    return true;
}

DELVE_TEST(animation_player_advances_time) {
    GltfAnimationClip clip{};
    clip.name = "idle";
    clip.duration = 2.0f;

    AnimationPlayer player;
    player.set_clip(&clip);
    player.update(0.5f);
    EXPECT_NEAR(player.get_time(), 0.5f, 1e-4f);
    player.update(0.5f);
    EXPECT_NEAR(player.get_time(), 1.0f, 1e-4f);
    return true;
}

DELVE_TEST(animation_player_loops_at_duration) {
    GltfAnimationClip clip{};
    clip.name = "walk";
    clip.duration = 1.0f;

    AnimationPlayer player;
    player.set_clip(&clip);
    player.update(0.8f);
    player.update(0.4f);
    EXPECT_NEAR(player.get_time(), 0.2f, 1e-3f);
    return true;
}

DELVE_TEST(animation_player_zero_duration_clip_stable) {
    GltfAnimationClip clip{};
    clip.name = "empty";
    clip.duration = 0.0f;

    AnimationPlayer player;
    player.set_clip(&clip);
    player.update(0.1f);
    float t = player.get_time();
    EXPECT_FALSE(std::isnan(t));
    return true;
}

DELVE_TEST(animation_player_null_clip_safe) {
    AnimationPlayer player;
    player.set_clip(nullptr);
    player.update(0.016f);
    EXPECT_FALSE(player.has_clip());
    return true;
}

DELVE_TEST(compute_bone_palette_identity_skeleton) {
    GltfSkeleton skel{};
    skel.root_bone_index = 0;

    for (int i = 0; i < 3; ++i) {
        GltfBone b{};
        b.name = "bone" + std::to_string(i);
        b.parent_index = i - 1;
        b.inverse_bind_matrix = glm::mat4(1.0f);
        b.local_rest_transform = glm::mat4(1.0f);
        skel.bones.push_back(b);
    }

    std::vector<BoneLocalTransform> locals(3);

    BonePalette palette = compute_bone_palette(skel, locals);

    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) {
                float expected = (c == r) ? 1.0f : 0.0f;
                EXPECT_NEAR(palette.bones[i][c][r], expected, 1e-5f);
            }
    }
    return true;
}

DELVE_TEST(compute_bone_palette_translation_propagates) {
    GltfSkeleton skel{};
    skel.root_bone_index = 0;

    GltfBone root{};
    root.name = "root";
    root.parent_index = -1;
    root.inverse_bind_matrix = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(5,0,0)));
    root.local_rest_transform = glm::translate(glm::mat4(1.0f), glm::vec3(5,0,0));
    skel.bones.push_back(root);

    GltfBone child{};
    child.name = "child";
    child.parent_index = 0;
    child.inverse_bind_matrix = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(5,0,0)));
    child.local_rest_transform = glm::mat4(1.0f);
    skel.bones.push_back(child);

    std::vector<BoneLocalTransform> locals(2);
    locals[0].translation = glm::vec3(7.0f, 0.0f, 0.0f);

    BonePalette palette = compute_bone_palette(skel, locals);

    EXPECT_NEAR(palette.bones[1][3][0], 2.0f, 1e-4f);
    EXPECT_NEAR(palette.bones[1][3][1], 0.0f, 1e-4f);
    EXPECT_NEAR(palette.bones[1][3][2], 0.0f, 1e-4f);
    return true;
}

DELVE_TEST(animation_player_sample_single_keyframe) {
    GltfAnimationClip clip{};
    clip.name = "test";
    clip.duration = 1.0f;

    GltfAnimChannel ch{};
    ch.bone_index = 0;
    ch.path = "translation";
    ch.times = {0.0f};
    ch.translations = {glm::vec3(1.0f, 2.0f, 3.0f)};
    clip.channels.push_back(ch);

    AnimationPlayer player;
    player.set_clip(&clip);
    player.update(0.0f);

    std::vector<BoneLocalTransform> locals(1);
    player.sample(locals);
    EXPECT_NEAR(locals[0].translation.x, 1.0f, 1e-4f);
    EXPECT_NEAR(locals[0].translation.y, 2.0f, 1e-4f);
    EXPECT_NEAR(locals[0].translation.z, 3.0f, 1e-4f);
    return true;
}

DELVE_TEST(animation_player_sample_lerps_translation) {
    GltfAnimationClip clip{};
    clip.name = "test";
    clip.duration = 2.0f;

    GltfAnimChannel ch{};
    ch.bone_index = 0;
    ch.path = "translation";
    ch.times = {0.0f, 2.0f};
    ch.translations = {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f)};
    clip.channels.push_back(ch);

    AnimationPlayer player;
    player.set_clip(&clip);
    player.update(1.0f);

    std::vector<BoneLocalTransform> locals(1);
    player.sample(locals);
    EXPECT_NEAR(locals[0].translation.x, 2.0f, 1e-3f);
    return true;
}
