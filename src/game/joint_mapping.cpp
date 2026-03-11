#include "joint_mapping.h"
#include <SDL3/SDL_log.h>
#include <unordered_map>
#include <string>

// Mixamo bone name → Joint enum mapping table
struct MixamoEntry { Joint joint; const char *name; const char *alt; };
static const MixamoEntry MIXAMO_TABLE[] = {
    { Joint::HIPS,        "mixamorig1:Hips",          "mixamorig:Hips" },
    { Joint::SPINE_01,    "mixamorig1:Spine",         "mixamorig:Spine" },
    { Joint::SPINE_02,    "mixamorig1:Spine1",        "mixamorig:Spine1" },
    { Joint::CHEST,       "mixamorig1:Spine2",        "mixamorig:Spine2" },
    { Joint::NECK,        "mixamorig1:Neck",          "mixamorig:Neck" },
    { Joint::HEAD,        "mixamorig1:Head",          "mixamorig:Head" },
    { Joint::HEAD_END,    "mixamorig1:HeadTop_End",   "mixamorig:HeadTop_End" },
    { Joint::L_CLAVICLE,  "mixamorig1:LeftShoulder",  "mixamorig:LeftShoulder" },
    { Joint::L_UPPER_ARM, "mixamorig1:LeftArm",       "mixamorig:LeftArm" },
    { Joint::L_LOWER_ARM, "mixamorig1:LeftForeArm",   "mixamorig:LeftForeArm" },
    { Joint::L_HAND,      "mixamorig1:LeftHand",      "mixamorig:LeftHand" },
    { Joint::R_CLAVICLE,  "mixamorig1:RightShoulder", "mixamorig:RightShoulder" },
    { Joint::R_UPPER_ARM, "mixamorig1:RightArm",      "mixamorig:RightArm" },
    { Joint::R_LOWER_ARM, "mixamorig1:RightForeArm",  "mixamorig:RightForeArm" },
    { Joint::R_HAND,      "mixamorig1:RightHand",     "mixamorig:RightHand" },
    { Joint::L_UPPER_LEG, "mixamorig1:LeftUpLeg",     "mixamorig:LeftUpLeg" },
    { Joint::L_LOWER_LEG, "mixamorig1:LeftLeg",       "mixamorig:LeftLeg" },
    { Joint::L_FOOT,      "mixamorig1:LeftFoot",      "mixamorig:LeftFoot" },
    { Joint::L_TOE,       "mixamorig1:LeftToeBase",   "mixamorig:LeftToeBase" },
    { Joint::R_UPPER_LEG, "mixamorig1:RightUpLeg",    "mixamorig:RightUpLeg" },
    { Joint::R_LOWER_LEG, "mixamorig1:RightLeg",      "mixamorig:RightLeg" },
    { Joint::R_FOOT,      "mixamorig1:RightFoot",     "mixamorig:RightFoot" },
    { Joint::R_TOE,       "mixamorig1:RightToeBase",  "mixamorig:RightToeBase" },
};
static constexpr int MIXAMO_TABLE_SIZE = sizeof(MIXAMO_TABLE) / sizeof(MIXAMO_TABLE[0]);

bool build_joint_mapping(JointMapping &mapping, const GltfSkinData &skin) {
    mapping.proc_to_gltf.fill(JOINT_UNMAPPED);
    mapping.gltf_joint_count = (uint32_t)skin.joints.size();
    mapping.gltf_to_proc.assign(mapping.gltf_joint_count, JOINT_UNMAPPED);
    mapping.mapped_count = 0;
    mapping.valid = false;

    // Build name → glTF index lookup
    std::unordered_map<std::string, int> name_to_idx;
    for (size_t i = 0; i < skin.joints.size(); ++i) {
        name_to_idx[skin.joints[i].name] = (int)i;
    }

    // Match each table entry
    for (int i = 0; i < MIXAMO_TABLE_SIZE; ++i) {
        const MixamoEntry &entry = MIXAMO_TABLE[i];
        int gltf_idx = JOINT_UNMAPPED;

        auto it = name_to_idx.find(entry.name);
        if (it != name_to_idx.end()) {
            gltf_idx = it->second;
        } else if (entry.alt) {
            it = name_to_idx.find(entry.alt);
            if (it != name_to_idx.end()) gltf_idx = it->second;
        }

        if (gltf_idx != JOINT_UNMAPPED) {
            mapping.proc_to_gltf[(int)entry.joint] = gltf_idx;
            mapping.gltf_to_proc[gltf_idx] = (int)entry.joint;
            mapping.mapped_count++;
        } else {
            SDL_Log("JointMapping: UNMAPPED joint %d (%s)", (int)entry.joint, entry.name);
        }
    }

    mapping.valid = mapping.mapped_count >= 14;
    SDL_Log("JointMapping: %d/%d joints mapped (valid=%s)",
            mapping.mapped_count, MIXAMO_TABLE_SIZE, mapping.valid ? "yes" : "no");
    return mapping.valid;
}

void compute_skinning_palette(
    const RigTransforms &xforms,
    const JointMapping &mapping,
    const SkeletonDef &skel,
    std::vector<glm::mat4> &palette)
{
    uint32_t n = mapping.gltf_joint_count;
    palette.resize(n);

    // Precompute bind-pose globals for unmapped joints
    // For unmapped bones: bind_global * inv_bind = identity
    // So palette[i] = identity for unmapped joints

    for (uint32_t i = 0; i < n; ++i) {
        int proc_idx = mapping.gltf_to_proc[i];
        if (proc_idx != JOINT_UNMAPPED && proc_idx < (int)Joint::POLE_KNEE_L) {
            // Mapped structural joint: use world-space bone mat4 directly
            palette[i] = xforms.bones[proc_idx] * skel.inverse_bind_matrices[i];
        } else {
            // Unmapped (finger bones, IK virtual, etc.): identity keeps bind pose
            palette[i] = glm::mat4(1.0f);
        }
    }
}
