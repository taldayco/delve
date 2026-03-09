#include "core/animator.h"
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
