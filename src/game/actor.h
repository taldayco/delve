#pragma once
#include <glm/glm.hpp>
#include <cstdint>

struct Player  {};
struct ActorTag {};

struct Transform {
    float x = 0, y = 0, z = 0;
    float facing = 0; // yaw in radians (0 = +X direction)
};

struct Velocity {
    float x = 0, y = 0, z = 0;
};

struct ActorConfig {
    float hip_width      = 0.25f;
    float shoulder_width = 0.35f;
    float leg_len        = 0.60f;
    float shin_len       = 0.55f;
    float torso_len      = 0.80f;
    float neck_len       = 0.15f;
    float head_radius    = 0.20f;
    float arm_len        = 0.35f;
    float forearm_len    = 0.30f;
    float limb_radius    = 0.07f;
    float torso_radius   = 0.14f;
};

struct ProceduralGait {
    float phase         = 0.0f;
    float stride_len    = 0.60f;
    float step_height   = 0.18f;
    float step_duration = 0.25f;
    float move_speed    = 4.0f;
};

enum class Joint : uint8_t {
    ROOT = 0,
    SPINE,
    CHEST,
    NECK,
    HEAD,
    L_SHOULDER, L_ELBOW, L_WRIST,
    R_SHOULDER, R_ELBOW, R_WRIST,
    L_HIP, L_KNEE, L_ANKLE,
    R_HIP, R_KNEE, R_ANKLE,
    COUNT
};

struct SkeletonPose {
    glm::vec3 joints[(int)Joint::COUNT] = {};
};

struct LegState {
    glm::vec3 foot[2]      = {};
    glm::vec3 prev_foot[2] = {};
    glm::vec3 target[2]    = {};
    float     progress[2]  = {};
    bool      stepping[2]  = {};
};

// Per-entity animation state — replaces static globals, ECS-copyable (plain floats)
struct AnimationState {
    // SmoothDamp spring state for velocity smoothing (critically-damped spring)
    float smooth_vel_x  = 0.0f;   // spring velocity for vx smoothing
    float smooth_vel_y  = 0.0f;   // spring velocity for vy smoothing
    float smoothed_vx   = 0.0f;   // current smoothed velocity x
    float smoothed_vy   = 0.0f;   // current smoothed velocity y
    // Replaced static globals from topo_game.cpp
    float sway_phase    = 0.0f;
    float sway_amt      = 0.04f;
    float lean_x        = 0.0f;
    float lean_y        = 0.0f;
    // Pendulum arm swing (antiphase: L=0, R=π)
    float arm_phase[2]     = {0.0f, 3.14159265f};
    float arm_delay_vel[2] = {0.0f, 0.0f};   // SmoothDamp spring vel for forearm lag
    // Idle micro-motion
    float breath_phase  = 0.0f;
    float weight_phase  = 0.0f;
};
