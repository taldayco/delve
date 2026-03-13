#pragma once
#include "../../engine/core/gltf_loader.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

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

// High-level wrapper: owns the skeleton reference, inverse bind matrices, and
// the active animation player. Callers call init() once, then update() each
// frame, then pass get_bone_palette() to the GPU uniform.
class SkeletalAnimation {
public:
    // Initialize from a loaded skinned asset. Copies the skeleton and inverse
    // bind matrices. Sets the first animation clip as the active clip (if any).
    void init(const GltfSkinnedAsset &asset);

    // Play a named animation clip. Resets playback time to 0.
    // Returns false if no clip with that name exists.
    bool play(const std::string &clip_name);

    // Advance time and recompute the bone palette.
    // root_transform is applied to all bones (encodes world position/facing/scale).
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
