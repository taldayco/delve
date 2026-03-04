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

// All mutable per-entity animation state (ECS component — must be trivially copyable).
// Replaces the static globals in topo_game.cpp and holds all fluid animation state.
struct AnimationState {
    // Velocity smoothing (SmoothDamp state)
    glm::vec2 smooth_vel      = {0.0f, 0.0f};
    glm::vec2 vel_vel         = {0.0f, 0.0f}; // spring velocity for smooth_vel
    glm::vec2 prev_smooth_vel = {0.0f, 0.0f};

    // Torso lean driven by acceleration
    float lean_x = 0.0f;
    float lean_y = 0.0f;

    // Hip sway
    float sway_phase = 0.0f;
    float sway_amt   = 0.04f;

    // Pendulum arm swing phases in radians, advance at gait rate.
    // arm_phase[1] starts at π so arms are 180° antiphase (contralateral gait).
    float arm_phase[2]     = {0.0f, 3.14159265f};
    // SmoothDamp spring velocities for arm delay
    float arm_delay_vel[2] = {0.0f, 0.0f};
    // Smoothed arm delay values (monotonic, lag behind arm_phase).
    // Also initialized antiphase (π offset) to match arm_phase.
    float arm_delay[2]     = {0.0f, 3.14159265f};

    // Idle micro-motion
    float breath_phase = 0.0f;
    float weight_phase = 0.0f;

    // Foot contact velocity at moment of plant (Subtask 6)
    float foot_contact_velocity[2] = {0.0f, 0.0f};
};
