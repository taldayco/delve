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

// Empirical vertical scale: reduces character world-Z height by ~18% so the
// actor reads as proportional to hex columns at HEX_SIZE=8.0 and HS=12.5 projection scale.
inline constexpr float ISO_VERT_SCALE = 0.8165f;

struct ActorConfig {
    float hip_width      = 0.25f;
    float shoulder_width = 0.35f;
    float leg_len        = 0.490f;  // 0.60 * ISO_VERT_SCALE
    float shin_len       = 0.449f;  // 0.55 * ISO_VERT_SCALE
    float torso_len      = 0.653f;  // 0.80 * ISO_VERT_SCALE
    float neck_len       = 0.122f;  // 0.15 * ISO_VERT_SCALE
    float head_radius    = 0.163f;  // 0.20 * ISO_VERT_SCALE
    float arm_len        = 0.286f;  // 0.35 * ISO_VERT_SCALE
    float forearm_len    = 0.245f;  // 0.30 * ISO_VERT_SCALE
    float limb_radius    = 0.07f;
    float torso_radius   = 0.14f;
};

struct ProceduralGait {
    float phase         = 0.0f;
    float stride_len    = 0.60f;
    float step_height   = 0.147f;  // 0.18 * ISO_VERT_SCALE
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

    // --- Hip counter-animation (procedural sway fix 3) ---
    float hip_roll      = 0.0f;  // lateral hip roll (radians)
    float hip_roll_rate = 0.0f;  // smooth_damp derivative
    float hip_bob       = 0.0f;  // vertical double-bounce offset (world units)
    float hip_bob_rate  = 0.0f;  // smooth_damp derivative
};