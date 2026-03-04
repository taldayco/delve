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
inline float gait_step_height(const ProceduralGait &g)   { return g.step_height; }

// --- New metrics for biomechanical animation ---

// Returns 1.0 if left arm and left leg are in anti-phase, 0.0 if in-phase.
// l_arm_angle: arm swing angle (+ = forward). l_leg_forward: foot forward offset from hip.
// Anti-phase: product is negative (opposite signs).
inline float arm_phase_opposition(float l_arm_angle, float l_leg_forward) {
  float product = l_arm_angle * l_leg_forward;
  return product < 0.0f ? 1.0f : 0.0f;
}

// Returns wrist_lag / shoulder_lag. Should be > 1.0 (wrist lags more than shoulder).
// Returns 1.0 if shoulder_lag is near zero (degenerate case).
inline float joint_lag_ratio(float shoulder_lag, float wrist_lag) {
  if (shoulder_lag < 1e-6f) return 1.0f;
  return wrist_lag / shoulder_lag;
}

// Returns the foot contact velocity from AnimationState for the given leg (0=left, 1=right).
// Lower is better: 0 = perfect plant, high values indicate skating.
inline float foot_contact_velocity(const AnimationState &anim, int leg) {
  return anim.foot_contact_velocity[leg];
}

// Returns correlation in [0, 1] between lean magnitude and acceleration magnitude.
// High correlation (> 0.7) means lean tracks acceleration well.
// max_lean: the maximum expected lean magnitude in radians.
inline float lean_acceleration_correlation(float lean_mag, float accel_mag, float max_lean) {
  if (max_lean < 1e-6f) return 1.0f;
  float expected = std::min(accel_mag * 0.015f, max_lean);
  float error    = std::abs(lean_mag - expected);
  return 1.0f - std::min(1.0f, error / max_lean);
}
