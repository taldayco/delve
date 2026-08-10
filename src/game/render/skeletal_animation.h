#pragma once
#include "../../engine/core/gltf_loader.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

struct BoneLocalTransform {
    glm::vec3 translation = glm::vec3(0.f);
    glm::quat rotation    = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 scale       = glm::vec3(1.f);
};

struct BonePalette {
    glm::mat4 bones[65];
};

BonePalette compute_bone_palette(const GltfSkeleton &skel,
                                  const std::vector<BoneLocalTransform> &local_transforms,
                                  const glm::mat4 &root_transform = glm::mat4(1.f));

BoneLocalTransform rest_pose_local(const glm::mat4 &m);

class AnimationMixer {
public:
    void set_clip(const GltfAnimationClip *clip, float crossfade_duration = 0.25f);
    void update(float dt);
    void sample(const GltfSkeleton &skel, std::vector<BoneLocalTransform> &out) const;

    float get_normalized_time() const;
    bool  has_clip()           const { return current_clip_ != nullptr; }
    const GltfAnimationClip* get_current_clip() const { return current_clip_; }

    void set_playback_speed(float speed) { playback_speed_ = speed; }

    static void sample_clip(const GltfAnimationClip *clip, float time,
                            std::vector<BoneLocalTransform> &out);

private:
    const GltfAnimationClip *current_clip_  = nullptr;
    const GltfAnimationClip *outgoing_clip_ = nullptr;
    float current_time_  = 0.f;
    float outgoing_time_ = 0.f;
    float blend_alpha_   = 1.f;
    float blend_duration_ = 0.25f;
    float playback_speed_ = 1.f;
};
