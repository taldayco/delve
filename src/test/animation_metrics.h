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
  // Compare left/right joint pairs
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
    // Mirror on X axis — left and right should be symmetric
    glm::vec3 mirrored_r = glm::vec3(-r.x, r.y, r.z);
    total_diff += glm::length(l - mirrored_r);
  }
  // Normalize: perfect symmetry = 1.0, high asymmetry → 0
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

// Simulate critically-damped spring: returns steps to reach 95% of target.
// Returns -1 if not converged within max_steps.
inline int smooth_velocity_convergence(float initial, float target,
                                        float smooth_time, float dt,
                                        int max_steps = 600) {
    float current = initial;
    float vel     = 0.f;
    float threshold = std::abs(target - initial) * 0.05f + 1e-6f;
    for (int i = 0; i < max_steps; ++i) {
        float omega  = 2.f / smooth_time;
        float x      = omega * dt;
        float exp_x  = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
        float delta  = current - target;
        float temp   = (vel + omega * delta) * dt;
        vel     = (vel - omega * temp) * exp_x;
        current = target + (delta + temp) * exp_x;
        if (std::abs(current - target) <= threshold)
            return i + 1;
    }
    return -1;
}

inline bool torso_lean_proportional(float lean_at_slow, float lean_at_fast) {
    return std::abs(lean_at_fast) > std::abs(lean_at_slow);
}

// Returns true if arm phases (in radians) are antiphase (|diff mod 2π - π| < tolerance_rad).
// Default tolerance is 0.15 * 2π ≈ 54°.
inline bool arm_swing_antiphase(float left_phase, float right_phase,
                                 float tolerance_rad = 0.15f * 6.28318530f) {
    constexpr float two_pi = 6.28318530f;
    constexpr float pi     = 3.14159265f;
    float diff = std::fmod(std::abs(left_phase - right_phase), two_pi);
    return std::abs(diff - pi) <= tolerance_rad;
}

inline bool breathing_amplitude_reasonable(float amplitude) {
    return amplitude >= 0.001f && amplitude <= 0.05f;
}

inline bool foot_planted_invariant(const LegState &legs) {
    return !(legs.stepping[0] && legs.stepping[1]);
}

inline bool step_duration_adaptive(float dur_slow, float dur_fast) {
    return dur_fast < dur_slow;
}
