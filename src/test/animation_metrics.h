#pragma once
#include "actor.h"
#include <cmath>
#include <glm/glm.hpp>

inline float joint_angle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
  glm::vec3 ba = glm::normalize(a - b);
  glm::vec3 bc = glm::normalize(c - b);
  float dot = glm::clamp(glm::dot(ba, bc), -1.0f, 1.0f);
  return glm::degrees(std::acos(dot));
}

inline float joint_distance(const glm::vec3 &a, const glm::vec3 &b) {
  return glm::length(a - b);
}

inline glm::vec3 joint_direction(const glm::vec3 &from, const glm::vec3 &to) {
  return glm::normalize(to - from);
}

inline float pose_symmetry_score(const SkeletonPose &pose) {
  struct Pair { Joint left; Joint right; };
  static const Pair pairs[] = {
    {Joint::L_SHOULDER, Joint::R_SHOULDER},
    {Joint::L_ELBOW,    Joint::R_ELBOW},
    {Joint::L_WRIST,    Joint::R_WRIST},
    {Joint::L_HIP,      Joint::R_HIP},
    {Joint::L_KNEE,     Joint::R_KNEE},
    {Joint::L_ANKLE,    Joint::R_ANKLE},
  };

  float total_diff = 0;
  for (auto &p : pairs) {
    glm::vec3 l = pose.joints[(int)p.left];
    glm::vec3 r = pose.joints[(int)p.right];
    glm::vec3 mirrored_r = glm::vec3(-r.x, r.y, r.z);
    total_diff += glm::length(l - mirrored_r);
  }
  return 1.0f / (1.0f + total_diff);
}

inline float skeleton_height(const SkeletonPose &pose) {
  float max_y = pose.joints[0].y, min_y = pose.joints[0].y;
  for (int i = 0; i < (int)Joint::COUNT; ++i) {
    max_y = std::max(max_y, pose.joints[i].y);
    min_y = std::min(min_y, pose.joints[i].y);
  }
  return max_y - min_y;
}

inline float gait_stride_length(const ProceduralGait &g) { return g.stride_len; }
inline float gait_step_height(const ProceduralGait &g) { return g.step_height; }

// ---------------------------------------------------------------------------
// New metric helpers for fluid animation tests (headless, no SDL)
// ---------------------------------------------------------------------------

// Mirrors the SmoothDamp from actor_animation.cpp for headless testing.
inline float smooth_damp_test(float current, float target, float &vel,
                               float smoothing_time, float dt) {
    float omega   = 2.0f / smoothing_time;
    float x       = omega * dt;
    float exp_val = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change  = current - target;
    float temp    = (vel + omega * change) * dt;
    vel           = (vel - omega * temp) * exp_val;
    return target + (change + temp) * exp_val;
}

// Ratio of smoothed speed to raw speed [0,1]. 1.0 = fully smoothed (idle is 1.0).
inline float velocity_smoothness(float raw_speed, float smooth_speed) {
    if (raw_speed < 1e-6f) return 1.0f;
    return std::min(smooth_speed / raw_speed, 1.0f);
}

// Maximum forward projection of elbow from shoulder, for either arm.
inline float arm_swing_amplitude(const SkeletonPose &pose, const Transform &t) {
    using J = Joint;
    float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);

    auto project_fwd = [&](Joint elbow_j, Joint shoulder_j) {
        const auto &e = pose.joints[(int)elbow_j];
        const auto &s = pose.joints[(int)shoulder_j];
        float dx = e.x - s.x, dy = e.y - s.y;
        return dx * fwd_x + dy * fwd_y;
    };

    float l = std::abs(project_fwd(J::L_ELBOW, J::L_SHOULDER));
    float r = std::abs(project_fwd(J::R_ELBOW, J::R_SHOULDER));
    return std::max(l, r);
}

// Returns the nominal breathing amplitude constant used in SkeletonFinaliseSystem.
inline float breathing_amplitude() {
    return 0.012f;
}

// Returns the speed-adaptive step duration.
// At speed=0: 0.45s, at speed=move_speed: 0.22s.
inline float step_duration_at_speed(float speed, float move_speed) {
    float speed_t = (move_speed > 1e-6f) ? std::min(speed / move_speed, 1.0f) : 0.0f;
    return 0.45f - speed_t * (0.45f - 0.22f);
}
