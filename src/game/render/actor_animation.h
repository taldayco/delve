#pragma once
#include "../actor.h"
#include <glm/glm.hpp>
#include <flecs.h>

struct AsyncTerrainState;

// Joint index shortcuts for skeletal animation
constexpr uint8_t J_HIP       = (uint8_t)Joint::ROOT;
constexpr uint8_t J_SPINE1    = (uint8_t)Joint::SPINE;
constexpr uint8_t J_SPINE2    = (uint8_t)Joint::CHEST;
constexpr uint8_t J_SPINE3    = (uint8_t)Joint::NECK;
constexpr uint8_t J_NECK      = (uint8_t)Joint::NECK;
constexpr uint8_t J_HEAD      = (uint8_t)Joint::HEAD;
constexpr uint8_t J_SHOULDER_L = (uint8_t)Joint::L_SHOULDER;
constexpr uint8_t J_ELBOW_L   = (uint8_t)Joint::L_ELBOW;
constexpr uint8_t J_WRIST_L   = (uint8_t)Joint::L_WRIST;
constexpr uint8_t J_SHOULDER_R = (uint8_t)Joint::R_SHOULDER;
constexpr uint8_t J_ELBOW_R   = (uint8_t)Joint::R_ELBOW;
constexpr uint8_t J_WRIST_R   = (uint8_t)Joint::R_WRIST;
constexpr uint8_t J_HIP_L     = (uint8_t)Joint::L_HIP;
constexpr uint8_t J_KNEE_L    = (uint8_t)Joint::L_KNEE;
constexpr uint8_t J_ANKLE_L   = (uint8_t)Joint::L_ANKLE;
constexpr uint8_t J_HIP_R     = (uint8_t)Joint::R_HIP;
constexpr uint8_t J_KNEE_R    = (uint8_t)Joint::R_KNEE;
constexpr uint8_t J_ANKLE_R   = (uint8_t)Joint::R_ANKLE;

// Animation state component for procedural animation tracking
struct AnimationState {
    // Velocity smoothing (for arm swing)
    glm::vec2 smooth_vel      = {0.f, 0.f};
    glm::vec2 vel_vel         = {0.f, 0.f};  // velocity for smooth_damp
    glm::vec2 prev_smooth_vel = {0.f, 0.f};

    // Arm swing
    float arm_phase[2]  = {0.f, 0.f};
    float arm_delay[2]  = {0.f, 0.f};

    // Sway (hip lateral motion during walk)
    float sway_phase = 0.f;
    float sway_amt   = 0.f;

    // Lean (torso forward/backward pitch from acceleration)
    float lean_x = 0.f;
    float lean_y = 0.f;

    // Breathing (idle micro-motion)
    float breath_phase = 0.f;

    // Weight-shift (idle micro-motion)
    float weight_phase = 0.f;
};

// Register all animation systems with the ECS world
void register_animation_systems(flecs::world& world, AsyncTerrainState* async_state);
