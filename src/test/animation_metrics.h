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

// Mirrors the SmoothDamp from actor_animation.cpp — for headless testing
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

// Run N steps of SmoothDamp and return (final_value - target) / (start - target)
// i.e., the residual fraction. 0 = converged, 1 = no change.
inline float smooth_damp_residual(float start, float target, float smoothing_time,
                                   float dt, int steps) {
    float val = start;
    float vel = 0.0f;
    for (int i = 0; i < steps; ++i)
        val = smooth_damp_test(val, target, vel, smoothing_time, dt);
    float denom = fabsf(start - target);
    if (denom < 1e-8f) return 0.0f;
    return fabsf(val - target) / denom;
}

// Returns true if arm phases are approximately PI apart (antiphase)
inline bool arm_phases_antiphase(float phase_a, float phase_b, float tolerance_rad = 0.1f) {
    float diff = fabsf(phase_a - phase_b);
    // Normalize to [0, 2*pi]
    float two_pi = 2.0f * 3.14159265f;
    diff = fmodf(diff, two_pi);
    if (diff > 3.14159265f) diff = two_pi - diff;
    return fabsf(diff - 3.14159265f) < tolerance_rad;
}

// Returns true if breathing amplitude is in physiologically reasonable range
inline bool breathing_amplitude_valid(float amplitude) {
    return amplitude >= 0.001f && amplitude <= 0.05f;
}

// Returns true if foot planted invariant holds: not both feet stepping simultaneously
inline bool foot_planted_invariant_holds(const LegState &legs) {
    return !(legs.stepping[0] && legs.stepping[1]);
}

// Speed-adaptive step duration: interpolate between slow_dur and fast_dur
// speed in [0, max_speed], result in [slow_dur, fast_dur]
inline float adaptive_step_duration(float speed, float max_speed,
                                     float slow_dur = 0.45f, float fast_dur = 0.22f) {
    float t = std::min(speed / std::max(max_speed, 1e-6f), 1.0f);
    return slow_dur + (fast_dur - slow_dur) * t;
}

// Single-frame SmoothDamp moves fraction toward target: returns how much moved [0,1]
inline float smooth_damp_single_frame_fraction(float smoothing_time, float dt) {
    float val = 0.0f;
    float vel = 0.0f;
    float result = smooth_damp_test(val, 1.0f, vel, smoothing_time, dt);
    return result; // fraction moved toward target in one frame
}
