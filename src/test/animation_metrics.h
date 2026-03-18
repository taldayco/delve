#pragma once
#include "rig.h"
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

inline float pose_symmetry_score(const RigPose &pose) {
  struct Pair { Joint left; Joint right; };
  static const Pair pairs[] = {
    {Joint::L_UPPER_ARM,  Joint::R_UPPER_ARM},
    {Joint::L_LOWER_ARM,  Joint::R_LOWER_ARM},
    {Joint::L_HAND,       Joint::R_HAND},
    {Joint::L_UPPER_LEG,  Joint::R_UPPER_LEG},
    {Joint::L_LOWER_LEG,  Joint::R_LOWER_LEG},
    {Joint::L_FOOT,       Joint::R_FOOT},
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

inline float skeleton_height(const RigPose &pose) {
  float max_z = pose.joints[0].z, min_z = pose.joints[0].z;
  for (int i = 0; i < (int)Joint::COUNT; ++i) {
    max_z = std::max(max_z, pose.joints[i].z);
    min_z = std::min(min_z, pose.joints[i].z);
  }
  return max_z - min_z;
}

inline float gait_stride_length(const ProceduralGait &g) { return g.stride_len; }
inline float gait_step_height(const ProceduralGait &g)   { return g.step_height; }

inline float arm_phase_opposition(float l_arm_angle, float l_leg_forward) {
  float product = l_arm_angle * l_leg_forward;
  return product < 0.0f ? 1.0f : 0.0f;
}

inline float joint_lag_ratio(float shoulder_lag, float wrist_lag) {
  if (shoulder_lag < 1e-6f) return 1.0f;
  return wrist_lag / shoulder_lag;
}

inline float foot_contact_velocity(const RigState &anim, int leg) {
  return anim.foot_contact_velocity[leg];
}

inline float lean_acceleration_correlation(float lean_mag, float accel_mag, float max_lean) {
  if (max_lean < 1e-6f) return 1.0f;
  float expected = std::min(accel_mag * 0.015f, max_lean);
  float error    = std::abs(lean_mag - expected);
  return 1.0f - std::min(1.0f, error / max_lean);
}
