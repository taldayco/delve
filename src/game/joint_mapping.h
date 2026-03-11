#pragma once
#include "rig.h"
#include "core/animator.h"
#include "core/gltf_loader.h"
#include <array>
#include <vector>

static constexpr int JOINT_UNMAPPED = -1;

struct JointMapping {
    std::array<int32_t, (int)Joint::COUNT> proc_to_gltf;  // Joint enum → glTF idx
    std::vector<int32_t> gltf_to_proc;                     // glTF idx → Joint enum (-1 if unmapped)
    uint32_t gltf_joint_count = 0;
    bool valid = false;
    int mapped_count = 0;
};

// Build mapping from Mixamo bone names in the skin
bool build_joint_mapping(JointMapping &mapping, const GltfSkinData &skin);

// Compute skinning palette directly from world-space RigTransforms bones.
// skinning_palette[i] = world_bone_of_mapped_joint * inverse_bind_matrix[i]
// Unmapped joints get identity (bind pose rendering).
void compute_skinning_palette(
    const RigTransforms &xforms,
    const JointMapping &mapping,
    const SkeletonDef &skel,
    std::vector<glm::mat4> &palette);
