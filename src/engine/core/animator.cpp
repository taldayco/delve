#include "core/animator.h"
#include "core/gltf_loader.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>

glm::mat4 joint_transform_to_mat4(const JointTransform &t) {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), t.translation);
    glm::mat4 R = glm::mat4_cast(t.rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), t.scale);
    return T * R * S;
}

void compute_global_transforms(Animator &animator) {
    if (!animator.skeleton_def || !animator.dirty) return;

    const SkeletonDef &skel = *animator.skeleton_def;
    uint32_t count = skel.joint_count();
    if (count == 0) return;

    // Ensure output vectors are sized
    animator.skinning_palette.resize(count);

    // Temporary global matrices
    std::vector<glm::mat4> global(count);

    for (uint32_t i = 0; i < count; ++i) {
        glm::mat4 local_mat = joint_transform_to_mat4(animator.local_transforms[i]);
        int32_t parent = skel.parent_indices[i];
        global[i] = (parent < 0) ? local_mat : global[parent] * local_mat;
        animator.skinning_palette[i] = global[i] * skel.inverse_bind_matrices[i];
    }

    animator.dirty = false;
}

SkeletonDef build_skeleton_def(const GltfSkinData &skin) {
    SkeletonDef skel;
    uint32_t n = (uint32_t)skin.joints.size();
    skel.parent_indices.resize(n);
    skel.bind_pose.resize(n);
    skel.inverse_bind_matrices = skin.inverse_bind_matrices;
    skel.joint_names.resize(n);

    for (uint32_t i = 0; i < n; ++i) {
        const GltfSkinJoint &sj = skin.joints[i];
        skel.parent_indices[i] = sj.parent_index;
        skel.joint_names[i]    = sj.name;
        skel.bind_pose[i].translation = sj.translation;
        skel.bind_pose[i].scale       = sj.scale;
        // CRITICAL: glTF stores quaternions as (x,y,z,w); GLM expects (w,x,y,z)
        const glm::vec4 &r = sj.rotation;
        skel.bind_pose[i].rotation = glm::quat(r.w, r.x, r.y, r.z);
    }

    SDL_Log("build_skeleton_def: %u joints", n);
    return skel;
}
