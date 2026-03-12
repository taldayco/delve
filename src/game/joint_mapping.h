#pragma once
#include "rig.h"
#include "core/animator.h"
#include "core/gltf_loader.h"
#include <array>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

static constexpr int JOINT_UNMAPPED = -1;

struct JointMapping {
    // Bidirectional index maps
    std::array<int32_t, (int)Joint::COUNT> proc_to_gltf;  // Joint enum -> glTF idx
    std::vector<int32_t> gltf_to_proc;                     // glTF idx -> Joint enum
    uint32_t gltf_joint_count = 0;
    bool valid = false;
    int mapped_count = 0;

    // Precomputed bind-pose data (sized to gltf_joint_count)
    std::vector<glm::mat3> bind_global_rot;        // glTF bind-pose global rotations
    std::vector<glm::mat3> bind_local_rot;         // glTF bind-pose local rotations (quat->mat3)
    std::vector<glm::vec3> bind_local_translation; // glTF bind-pose local translations
    std::vector<glm::mat3> rot_offset;             // per-mapped: inv(proc_bind_rot) * gltf_bind_rot
};

// Build mapping + precompute bind-pose calibration data.
// bind_pose_xforms: procedural rig's rest-pose RigTransforms (captured at init).
bool build_joint_mapping(JointMapping &mapping,
                         const GltfSkinData &skin,
                         const SkeletonDef &skel,
                         const RigTransforms &bind_pose_xforms);

// Compute skinning palette via Global Rotation Retargeting.
// xforms: current frame's procedural world-space bone transforms.
void compute_skinning_palette(
    const RigTransforms &xforms,
    const JointMapping &mapping,
    const SkeletonDef &skel,
    std::vector<glm::mat4> &palette);

// Compute procedural rig rest-pose transforms from ActorConfig defaults.
// Pure function, no ECS dependency.
RigTransforms compute_procedural_bind_pose(const ActorConfig &cfg);
