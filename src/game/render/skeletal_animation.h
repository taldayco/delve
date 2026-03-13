#pragma once
#include "../../engine/core/gltf_loader.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

struct BoneLocalTransform {
    glm::vec3 translation = glm::vec3(0.f);
    glm::quat rotation    = glm::quat(1.f, 0.f, 0.f, 0.f); // identity
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

// Walk the bone hierarchy top-down and compute GPU-ready skin matrices.
// local_transforms[i] is the local transform for bone i.
// root_transform is pre-multiplied into every bone (use for player position/facing/scale).
// Result: BonePalette where bones[i] = root_transform * GlobalTransform[i] * InverseBind[i]
BonePalette compute_bone_palette(const GltfSkeleton &skel,
                                  const std::vector<BoneLocalTransform> &local_transforms,
                                  const glm::mat4 &root_transform = glm::mat4(1.f));
