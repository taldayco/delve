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

// Classical 8-head proportions at H=2.0 world units.
// Head unit h = H/8 = 0.25. Crotch at midpoint (4h from top = 4h from ground).
// upper_leg = lower_leg = H/4 = 0.50 (1:1 ratio, legs = half of height).
// torso (crotch→shoulders) = 3H/8 = 0.75.
// neck = H/32 = 0.0625, head_radius = H/16 = 0.125 (full head = H/8).
// shoulder_width (per side) = 3H/16 = 0.375, hip_width (per side) = H/8 = 0.25.
// arm_len (upper arm) = 3H/16 = 0.375, forearm_len = H/8 = 0.25.
struct ActorConfig {
    float hip_width      = 0.25f;  // H/8  — per side from center
    float shoulder_width = 0.375f; // 3H/16 — per side from center
    float leg_len        = 0.50f;  // H/4  — upper leg (hip to knee)
    float shin_len       = 0.50f;  // H/4  — lower leg (knee to ankle)
    float torso_len      = 0.75f;  // 3H/8 — crotch to shoulders
    float neck_len       = 0.0625f; // H/32 — visual neck cylinder
    float head_radius    = 0.125f; // H/16 — sphere (full head = H/8)
    float arm_len        = 0.375f; // 3H/16 — upper arm (shoulder to elbow)
    float forearm_len    = 0.25f;  // H/8  — forearm (elbow to wrist)
    float limb_radius    = 0.06f;
    float torso_radius   = 0.12f;
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

// All mutable per-actor animation state. Lives in ECS so it's copyable and
// accessible headlessly. Replaces the static globals in SkeletonFinaliseSystem.
struct AnimationState {
    // --- Velocity smoothing (SmoothDamp) ---
    glm::vec3 smooth_velocity{0.0f};   // current smoothed velocity
    glm::vec3 velocity_rate{0.0f};     // SmoothDamp internal derivative state

    // --- Hip sway (was s_sway_phase / s_sway_amt) ---
    float sway_phase  = 0.0f;
    float sway_amount = 0.04f;

    // --- Torso lean (was s_lean_x / s_lean_y) ---
    // Smoothed lean values per spine segment (successive breaking)
    float lean_x = 0.0f;
    float lean_y = 0.0f;
    // SmoothDamp derivative state for each spine segment lean
    float chest_lean_x_rate = 0.0f;
    float chest_lean_y_rate = 0.0f;
    float neck_lean_x_rate  = 0.0f;
    float neck_lean_y_rate  = 0.0f;
    float head_lean_x_rate  = 0.0f;
    float head_lean_y_rate  = 0.0f;
    // Per-segment smoothed lean values
    float chest_lean_x = 0.0f;
    float chest_lean_y = 0.0f;
    float neck_lean_x  = 0.0f;
    float neck_lean_y  = 0.0f;
    float head_lean_x  = 0.0f;
    float head_lean_y  = 0.0f;

    // --- Arm swing (pendulum) ---
    float l_arm_target      = 0.0f;   // target swing angle for left arm
    float r_arm_target      = 0.0f;   // target swing angle for right arm
    float l_shoulder_smooth = 0.0f;   // smoothed shoulder chain value
    float l_elbow_smooth    = 0.0f;   // smoothed elbow chain value (lags shoulder)
    float l_wrist_smooth    = 0.0f;   // smoothed wrist chain value (lags elbow)
    float r_shoulder_smooth = 0.0f;
    float r_elbow_smooth    = 0.0f;
    float r_wrist_smooth    = 0.0f;
    // SmoothDamp derivative states for arm joints
    float l_shoulder_rate   = 0.0f;
    float l_elbow_rate      = 0.0f;
    float l_wrist_rate      = 0.0f;
    float r_shoulder_rate   = 0.0f;
    float r_elbow_rate      = 0.0f;
    float r_wrist_rate      = 0.0f;

    // --- Idle micro-motion ---
    float breath_phase    = 0.0f;   // free-running breathing phase (rad)
    float idle_sway_phase = 0.0f;   // free-running idle weight-shift phase (rad)

    // --- Grounding: foot contact quality ---
    float foot_contact_velocity[2] = {0.0f, 0.0f};

    // --- Previous velocity for acceleration computation ---
    glm::vec3 prev_velocity{0.0f};
};
