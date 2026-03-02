#pragma once
#include "actor.h"
#include <cmath>
#include <glm/glm.hpp>

// Angle between two joints in degrees
inline float joint_angle(const SkeletonPose& pose, Joint a, Joint b, Joint c) {
  glm::vec3 va = pose.joints[(int)a];
  glm::vec3 vb = pose.joints[(int)b];
  glm::vec3 vc = pose.joints[(int)c];

  glm::vec3 ab = glm::normalize(va - vb);
  glm::vec3 cb = glm::normalize(vc - vb);

  float dot = glm::clamp(glm::dot(ab, cb), -1.0f, 1.0f);
  return glm::degrees(std::acos(dot));
}

// Distance between two joints
inline float joint_distance(const SkeletonPose& pose, Joint a, Joint b) {
  return glm::length(pose.joints[(int)a] - pose.joints[(int)b]);
}

// Direction vector from one joint to another (normalized)
inline glm::vec3 joint_direction(const SkeletonPose& pose, Joint from, Joint to) {
  glm::vec3 diff = pose.joints[(int)to] - pose.joints[(int)from];
  float len = glm::length(diff);
  if (len < 1e-6f) return glm::vec3(0);
  return diff / len;
}

// Symmetry score: how similar are left/right joint positions relative to center
// Returns 0.0 (asymmetric) to 1.0 (perfectly symmetric)
inline float pose_symmetry_score(const SkeletonPose& pose) {
  // Mirror pairs: L↔R for shoulder, elbow, wrist, hip, knee, ankle
  const Joint pairs[][2] = {
    {Joint::L_SHOULDER, Joint::R_SHOULDER},
    {Joint::L_ELBOW,    Joint::R_ELBOW},
    {Joint::L_WRIST,    Joint::R_WRIST},
    {Joint::L_HIP,      Joint::R_HIP},
    {Joint::L_KNEE,     Joint::R_KNEE},
    {Joint::L_ANKLE,    Joint::R_ANKLE},
  };

  glm::vec3 center = pose.joints[(int)Joint::ROOT];
  float total_diff = 0;
  float total_dist = 0;

  for (auto& [l, r] : pairs) {
    glm::vec3 lp = pose.joints[(int)l] - center;
    glm::vec3 rp = pose.joints[(int)r] - center;

    // Mirror right across X axis for comparison
    glm::vec3 rp_mirrored = glm::vec3(-rp.x, rp.y, rp.z);

    total_diff += glm::length(lp - rp_mirrored);
    total_dist += (glm::length(lp) + glm::length(rp)) * 0.5f;
  }

  if (total_dist < 1e-6f) return 1.0f;
  float asymmetry = total_diff / total_dist;
  return std::max(0.0f, 1.0f - asymmetry);
}

// Total height of the skeleton (head to ankle)
inline float skeleton_height(const SkeletonPose& pose) {
  float max_y = -1e30f, min_y = 1e30f;
  for (int i = 0; i < (int)Joint::COUNT; i++) {
    max_y = std::max(max_y, pose.joints[i].y);
    min_y = std::min(min_y, pose.joints[i].y);
  }
  return max_y - min_y;
}

// Stride length from gait parameters
inline float gait_stride_length(const ProceduralGait& gait) {
  return gait.stride_len;
}

// Step height from gait parameters
inline float gait_step_height(const ProceduralGait& gait) {
  return gait.step_height;
}
