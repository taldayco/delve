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
// New metrics for fluid animation features
// ---------------------------------------------------------------------------

// Simulate a critically-damped SmoothDamp for N steps.
// Returns the residual |current - target| after all steps.
inline float smooth_damp_residual(float start, float target,
                                   float smooth_time, int steps, float dt) {
    float vel     = 0.0f;
    float current = start;
    float omega   = 2.0f / smooth_time;
    for (int i = 0; i < steps; ++i) {
        float x      = omega * dt;
        float ef     = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        float change = current - target;
        float temp   = (vel + omega * change) * dt;
        vel          = (vel - omega * temp) * ef;
        current      = target + (change + temp) * ef;
    }
    return std::abs(current - target);
}

// True if phase_l and phase_r are approximately π apart (modulo 2π).
inline bool arm_phases_antiphase(float phase_l, float phase_r) {
    const float two_pi = 6.28318530f;
    const float pi     = 3.14159265f;
    float diff = std::abs(phase_l - phase_r);
    // Fold into [0, 2π)
    diff = diff - std::floor(diff / two_pi) * two_pi;
    // Accept if within 0.5 rad of π
    return std::abs(diff - pi) < 0.5f;
}

// True if the breathing amplitude is within the expected physiological range.
inline bool breathing_amplitude_valid(float amp) {
    return amp > 0.005f && amp < 0.05f;
}

// One-foot-planted invariant: at most one foot may be airborne at a time.
inline bool foot_planted_invariant(bool stepping_l, bool stepping_r) {
    return !(stepping_l && stepping_r);
}

// True if a slower speed gives a longer step duration (speed-adaptive).
inline bool adaptive_step_duration_decreases(float slow_dur, float fast_dur) {
    return slow_dur > fast_dur;
}

// True if spine lean fractions form a strictly increasing chain
// (successive breaking: spine < chest < neck).
inline bool torso_lean_successive(float lean_spine_frac,
                                   float lean_chest_frac,
                                   float lean_neck_frac) {
    return lean_spine_frac < lean_chest_frac
        && lean_chest_frac < lean_neck_frac;
}
