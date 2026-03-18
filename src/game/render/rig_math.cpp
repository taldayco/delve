#include "render/rig_math.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

void solve_two_bone(glm::vec3 H, glm::vec3 target,
                    float a, float b,
                    glm::vec3 pole, glm::vec3 fallback_perp,
                    glm::vec3 &out_mid, glm::vec3 &out_end) {
    out_end = target;

    glm::vec3 axis = target - H;
    float D = glm::length(axis);
    float min_D = fabsf(a - b) + 0.001f;
    float max_D = a + b - 0.001f;

    float stretch_limit = max_D * 1.15f;
    if (D > stretch_limit && D > 1e-5f)
        out_end = H + (axis / D) * stretch_limit;

    D = std::max(min_D, std::min(D, max_D));

    if (glm::length(axis) > 1e-5f)
        axis = glm::normalize(axis) * D;
    else
        axis = glm::vec3(0, 0, -D);

    float cos_alpha = (a * a + D * D - b * b) / (2.0f * a * D);
    cos_alpha = std::max(-1.0f, std::min(1.0f, cos_alpha));
    float alpha = acosf(cos_alpha);

    glm::vec3 axis_n = glm::normalize(axis);
    glm::vec3 pole_off = pole - H;
    glm::vec3 perp = pole_off - glm::dot(pole_off, axis_n) * axis_n;
    if (glm::length(perp) > 1e-5f)
        perp = glm::normalize(perp);
    else
        perp = fallback_perp;

    glm::vec3 dir_to_mid = axis_n * cosf(alpha) + perp * sinf(alpha);
    out_mid = H + dir_to_mid * a;
}

void compute_derived_joints(RigPose &pose, const ActorConfig &cfg,
                            float visual_facing, float chest_facing,
                            const glm::vec3 &root_pos) {
    using J = Joint;

    pose.joints[(int)J::ROOT] = root_pos;

    pose.joints[(int)J::SPINE_02] = glm::mix(
        pose.joints[(int)J::SPINE_01],
        pose.joints[(int)J::CHEST],
        0.6f);

    pose.joints[(int)J::HEAD_END] = pose.joints[(int)J::HEAD]
        + glm::vec3(0.0f, 0.0f, cfg.head_radius);

    pose.joints[(int)J::L_CLAVICLE] = glm::mix(
        pose.joints[(int)J::CHEST],
        pose.joints[(int)J::L_UPPER_ARM],
        0.25f);
    pose.joints[(int)J::R_CLAVICLE] = glm::mix(
        pose.joints[(int)J::CHEST],
        pose.joints[(int)J::R_UPPER_ARM],
        0.25f);

    float fwd_x = cosf(visual_facing), fwd_y = sinf(visual_facing);
    glm::vec3 facing_dir(fwd_x, fwd_y, 0.0f);
    pose.joints[(int)J::L_TOE] = pose.joints[(int)J::L_FOOT]
        + facing_dir * cfg.toe_len;
    pose.joints[(int)J::R_TOE] = pose.joints[(int)J::R_FOOT]
        + facing_dir * cfg.toe_len;

    glm::vec3 fwd3(fwd_x, fwd_y, 0.0f);
    pose.joints[(int)J::POLE_KNEE_L]  = pose.joints[(int)J::L_LOWER_LEG] + fwd3 * 0.25f;
    pose.joints[(int)J::POLE_KNEE_R]  = pose.joints[(int)J::R_LOWER_LEG] + fwd3 * 0.25f;

    float cf_x = cosf(chest_facing), cf_y = sinf(chest_facing);
    glm::vec3 chest_fwd3(cf_x, cf_y, 0.0f);
    pose.joints[(int)J::POLE_ELBOW_L] = pose.joints[(int)J::L_LOWER_ARM] - chest_fwd3 * 0.25f;
    pose.joints[(int)J::POLE_ELBOW_R] = pose.joints[(int)J::R_LOWER_ARM] - chest_fwd3 * 0.25f;

    pose.joints[(int)J::IK_FOOT_L] = pose.joints[(int)J::L_FOOT];
    pose.joints[(int)J::IK_FOOT_R] = pose.joints[(int)J::R_FOOT];
    pose.joints[(int)J::IK_HAND_L] = pose.joints[(int)J::L_HAND];
    pose.joints[(int)J::IK_HAND_R] = pose.joints[(int)J::R_HAND];
}
