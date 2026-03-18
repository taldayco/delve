#pragma once
#include "../../engine/core/gltf_loader.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

struct BoneLocalTransform {
    glm::vec3 translation = glm::vec3(0.f);
    glm::quat rotation    = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 scale       = glm::vec3(1.f);
};

struct BonePalette {
    glm::mat4 bones[65];
};

class AnimationPlayer {
public:
    void set_clip(const GltfAnimationClip *clip);
    void update(float dt);
    void sample(std::vector<BoneLocalTransform> &out) const;

    float get_time() const { return time_; }
    bool  has_clip() const { return clip_ != nullptr; }
    const GltfAnimationClip* get_clip() const { return clip_; }

private:
    const GltfAnimationClip *clip_ = nullptr;
    float                    time_ = 0.f;
};

BonePalette compute_bone_palette(const GltfSkeleton &skel,
                                  const std::vector<BoneLocalTransform> &local_transforms,
                                  const glm::mat4 &root_transform = glm::mat4(1.f));

class SkeletalAnimation {
public:
    void init(const GltfSkinnedAsset &asset);

    bool play(const std::string &clip_name);

    void update(float dt, const glm::mat4 &root_transform = glm::mat4(1.f));

    const BonePalette &get_bone_palette() const { return bone_palette_; }
    bool               has_skeleton()     const { return !skeleton_.bones.empty(); }
    int                bone_count()       const { return (int)skeleton_.bones.size(); }

private:
    GltfSkeleton                     skeleton_;
    std::vector<glm::mat4>           inverse_bind_matrices_;
    std::vector<GltfAnimationClip>   clips_;
    AnimationPlayer                  player_;
    BonePalette                      bone_palette_{};
};
