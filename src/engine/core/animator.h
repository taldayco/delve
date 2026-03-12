#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

static constexpr uint32_t MAX_SKINNING_JOINTS = 128;

struct JointTransform {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1, 0, 0, 0};  // identity (w,x,y,z)
    glm::vec3 scale{1.0f};
};

struct SkeletonDef {
    std::vector<int32_t>        parent_indices;  // -1 for roots
    std::vector<JointTransform> bind_pose;
    std::vector<glm::mat4>      inverse_bind_matrices;
    std::vector<std::string>    joint_names;
    uint32_t joint_count() const { return (uint32_t)parent_indices.size(); }
};

struct Animator {
    std::vector<JointTransform> local_transforms;
    std::vector<glm::mat4>      skinning_palette;  // output: global * inv_bind
    const SkeletonDef          *skeleton_def = nullptr;
    bool                        dirty = true;
};

glm::mat4 joint_transform_to_mat4(const JointTransform &t);
void compute_global_transforms(Animator &animator);

struct GltfSkinData;
SkeletonDef build_skeleton_def(const GltfSkinData &skin);
