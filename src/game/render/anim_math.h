#pragma once
#include "skeletal_animation.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

float smooth_damp(float current, float target, float *velocity,
                  float smooth_time, float dt);

float smooth_damp_angle(float current, float target, float *velocity,
                        float smooth_time, float dt);

void solve_two_bone(glm::vec3 H, glm::vec3 target,
                    float a, float b,
                    glm::vec3 pole, glm::vec3 fallback_perp,
                    glm::vec3 &out_mid, glm::vec3 &out_end);

void blend_local_transforms(const BoneLocalTransform *a,
                            const BoneLocalTransform *b,
                            float alpha,
                            BoneLocalTransform *out,
                            int count);

void additive_rotation(BoneLocalTransform &base, const glm::quat &delta, float weight);

void additive_translation(BoneLocalTransform &base, const glm::vec3 &offset, float weight);

// Legacy utilities — retained for animation tests
struct RigPose;
struct ActorConfig;

inline glm::mat4 make_bone_mat4(const glm::vec3 &right,
                                 const glm::vec3 &fwd,
                                 const glm::vec3 &up,
                                 const glm::vec3 &pos) {
    return glm::mat4(
        glm::vec4(right, 0.0f),
        glm::vec4(fwd,   0.0f),
        glm::vec4(up,    0.0f),
        glm::vec4(pos,   1.0f)
    );
}

inline void build_bone_basis(const glm::vec3 &bone_dir,
                              const glm::vec3 &ref_fwd,
                              glm::vec3 &out_right,
                              glm::vec3 &out_fwd,
                              glm::vec3 &out_up) {
    float len = glm::length(bone_dir);
    if (len < 1e-5f) {
        out_right = glm::vec3(1.0f, 0.0f, 0.0f);
        out_fwd   = glm::vec3(0.0f, 1.0f, 0.0f);
        out_up    = glm::vec3(0.0f, 0.0f, 1.0f);
        return;
    }
    out_up = bone_dir / len;

    out_right = glm::cross(ref_fwd, out_up);
    float right_len = glm::length(out_right);
    if (right_len < 1e-5f) {
        glm::vec3 alt = (std::abs(out_up.z) < 0.9f)
                            ? glm::vec3(0.0f, 0.0f, 1.0f)
                            : glm::vec3(1.0f, 0.0f, 0.0f);
        out_right = glm::normalize(glm::cross(alt, out_up));
    } else {
        out_right /= right_len;
    }

    out_fwd = glm::cross(out_up, out_right);
}

void compute_derived_joints(RigPose &pose, const ActorConfig &cfg,
                            float visual_facing, float chest_facing,
                            const glm::vec3 &root_pos);
