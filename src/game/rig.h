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
    float leg_len        = 0.366f;   // thigh (0.430 * 0.85)
    float shin_len       = 0.332f;   // shin  (0.390 * 0.85)
    float torso_len      = 0.520f;   // torso (0.612 * 0.85)
    float neck_len       = 0.104f;   // neck  (0.122 * 0.85)
    float head_radius    = 0.042f;   // head  (0.163 * 0.85 * 0.3)
    float arm_len        = 0.270f;   // upper arm (0.318 * 0.85)
    float forearm_len    = 0.230f;   // forearm   (0.270 * 0.85)
    float leg_radius     = 0.021f;   // 0.07 * 0.3
    float arm_radius     = 0.017f;   // 0.055 * 0.3
    float torso_radius   = 0.027f;   // 0.09 * 0.3
    float toe_len        = 0.07f;    // distance from foot to toe tip
};

struct ProceduralGait {
    float phase         = 0.0f;
    float phase_target  = 0.0f;   // discrete target set by step events
    float phase_rate    = 0.0f;   // smooth_damp derivative for phase blending
    float stride_len    = 0.85f;
    float step_height   = 0.16f;   // proportional to stride length
    float step_duration = 0.25f;
    float move_speed    = 4.0f;
};

enum class Joint : uint8_t {
    // Core spine chain
    ROOT = 0,      // Ground anchor (at terrain Z below HIPS)
    HIPS,          // Pelvis center
    SPINE_01,      // Lower spine (~40% torso)
    SPINE_02,      // Mid spine (~70% torso)
    CHEST,         // Upper chest
    NECK,
    HEAD,
    HEAD_END,      // Head tip nub (HEAD + head_radius up)

    // Left arm chain
    L_CLAVICLE,    // Left clavicle (between CHEST and upper arm)
    L_UPPER_ARM,   // Left upper arm
    L_LOWER_ARM,   // Left lower arm
    L_HAND,        // Left hand

    // Right arm chain
    R_CLAVICLE,    // Right clavicle
    R_UPPER_ARM,   // Right upper arm
    R_LOWER_ARM,   // Right lower arm
    R_HAND,        // Right hand

    // Left leg chain
    L_UPPER_LEG,   // Left thigh
    L_LOWER_LEG,   // Left shin
    L_FOOT,        // Left ankle/foot
    L_TOE,         // Left toe tip

    // Right leg chain
    R_UPPER_LEG,   // Right thigh
    R_LOWER_LEG,   // Right shin
    R_FOOT,        // Right ankle/foot
    R_TOE,         // Right toe tip

    // IK virtual joints (not mesh-deforming, debug-only)
    POLE_KNEE_L,   // Left knee pole target
    POLE_KNEE_R,   // Right knee pole target
    POLE_ELBOW_L,  // Left elbow pole target
    POLE_ELBOW_R,  // Right elbow pole target
    IK_FOOT_L,     // Left foot IK goal
    IK_FOOT_R,     // Right foot IK goal
    IK_HAND_L,     // Left hand IK goal
    IK_HAND_R,     // Right hand IK goal

    COUNT          // = 32
};

struct RigPose {
    glm::vec3 joints[(int)Joint::COUNT] = {};
};

struct RigTransforms {
    glm::mat4 bones[(int)Joint::COUNT] = {};
};

// Assemble a bone-space mat4 from orthonormal basis vectors + position.
// Convention: col0=Right, col1=Forward, col2=Up, col3=Position(w=1).
inline glm::mat4 make_bone_mat4(const glm::vec3 &right,
                                 const glm::vec3 &fwd,
                                 const glm::vec3 &up,
                                 const glm::vec3 &pos) {
    return glm::mat4(
        glm::vec4(right, 0.0f),
        glm::vec4(fwd,   0.0f),
        glm::vec4(up,    0.0f),
        glm::vec4(pos,   1.0f)
    );
}

// Compute an orthonormal frame from a bone direction vector and a reference
// forward vector.  bone_dir becomes the "up" axis of the basis.
inline void build_bone_basis(const glm::vec3 &bone_dir,
                              const glm::vec3 &ref_fwd,
                              glm::vec3 &out_right,
                              glm::vec3 &out_fwd,
                              glm::vec3 &out_up) {
    float len = glm::length(bone_dir);
    if (len < 1e-5f) {
        // Degenerate direction — fallback to identity basis.
        out_right = glm::vec3(1.0f, 0.0f, 0.0f);
        out_fwd   = glm::vec3(0.0f, 1.0f, 0.0f);
        out_up    = glm::vec3(0.0f, 0.0f, 1.0f);
        return;
    }
    out_up = bone_dir / len;

    out_right = glm::cross(ref_fwd, out_up);
    float right_len = glm::length(out_right);
    if (right_len < 1e-5f) {
        // bone_dir is (anti-)parallel to ref_fwd — pick perpendicular fallback.
        glm::vec3 alt = (std::abs(out_up.z) < 0.9f)
                            ? glm::vec3(0.0f, 0.0f, 1.0f)
                            : glm::vec3(1.0f, 0.0f, 0.0f);
        out_right = glm::normalize(glm::cross(alt, out_up));
    } else {
        out_right /= right_len;
    }

    out_fwd = glm::cross(out_up, out_right);  // guaranteed orthonormal
}

struct LegState {
    glm::vec3 foot[2]      = {};
    glm::vec3 prev_foot[2] = {};
    glm::vec3 target[2]    = {};
    float     progress[2]  = {};
    bool      stepping[2]  = {};

    // World-locked plant positions
    glm::vec3 plant_pos[2] = {};
    bool      planted[2]   = {};

    // Turn-aware step priority (-1 = none)
    int       turn_step_queued = -1;

    int       last_step_leg = -1;  // Which leg stepped most recently (-1 = none yet)
};

// All mutable per-actor animation state. Lives in ECS so it's copyable and
// accessible headlessly. Replaces the static globals in SkeletonFinaliseSystem.
struct RigState {
    // --- Velocity smoothing (SmoothDamp) ---
    glm::vec3 smooth_velocity{0.0f};   // current smoothed velocity
    glm::vec3 velocity_rate{0.0f};     // SmoothDamp internal derivative state

    // --- Leg-driven support balance ---
    float support_balance      = 0.0f;  // [-1,+1]: -1=left planted, +1=right planted
    float support_balance_rate = 0.0f;  // smooth_damp derivative

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
    float l_shoulder_smooth = 0.0f;   // smoothed upper-arm chain value
    float l_elbow_smooth    = 0.0f;   // smoothed lower-arm chain value (lags upper)
    float l_wrist_smooth    = 0.0f;   // smoothed hand chain value (lags lower)
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
    float idle_sway_phase = 0.0f;   // free-running idle lateral sway phase (rad)
    float idle_weight_phase = 0.0f; // free-running weight-shift phase (rad, ~0.3 Hz)

    // --- Grounding: foot contact quality ---
    float foot_contact_velocity[2] = {0.0f, 0.0f};

    // --- Previous velocity for acceleration computation ---
    glm::vec3 prev_velocity{0.0f};

    // --- Hip counter-animation (procedural sway fix 3) ---
    float hip_roll      = 0.0f;  // lateral hip roll (radians)
    float hip_roll_rate = 0.0f;  // smooth_damp derivative
    float hip_bob       = 0.0f;  // vertical double-bounce offset (world units)
    float hip_bob_rate  = 0.0f;  // smooth_damp derivative

    // --- Step-event hip dip ---
    float hip_dip      = 0.0f;  // vertical dip during foot transition (world units)
    float hip_dip_rate = 0.0f;  // smooth_damp derivative

    // --- Slope-driven hip tilt (Phase 1C) ---
    float hip_tilt      = 0.0f;  // lateral tilt from foot height diff (radians)
    float hip_tilt_rate = 0.0f;  // smooth_damp derivative

    // --- Look-at smoothing (Phase 2) ---
    float look_yaw        = 0.0f;  // current smoothed look yaw (radians)
    float look_pitch      = 0.0f;  // current smoothed look pitch (radians)
    float look_yaw_rate   = 0.0f;  // smooth_damp derivative
    float look_pitch_rate = 0.0f;  // smooth_damp derivative

    // --- Visual body orientation (decoupled from t.facing for smooth turns) ---
    float visual_facing      = 0.0f;  // smoothed body orientation (radians)
    float visual_facing_rate = 0.0f;  // smooth_damp derivative

    // --- Turn detection state ---
    float turn_delta       = 0.0f;  // signed: t.facing - visual_facing, wrapped [-PI,PI]
    float turn_magnitude   = 0.0f;  // abs(turn_delta), [0, PI]
    float turn_urgency     = 0.0f;  // turn_magnitude / PI, [0, 1]
    bool  in_large_turn    = false; // hysteresis flag (enter > 60°, exit < 20°)

    // --- Chest counter-rotation ---
    float chest_facing      = 0.0f;  // smoothed upper-body orientation (lags visual_facing)
    float chest_facing_rate = 0.0f;  // smooth_damp derivative
};

// Hip counter-animation state for rendering pass.
// Computed from gait phase; used to apply procedural hip motion to skeleton.
struct RigHipState {
    float stride_phase      = 0.0f;  // [0, 1) normalized gait phase
    float hip_rotation_deg  = 0.0f;  // lateral hip sway (degrees)
    float hip_drop_fraction = 0.0f;  // fraction of max drop [0, hip_drop_max]
    float hip_bob_y         = 0.0f;  // vertical double-bounce offset (world units)
};

// Configuration for hip counter-animation in rendering.
struct AnimationConfig {
    // Isometric height scale: squashes character for 2:1 iso proportions.
    static constexpr float ISO_CHAR_HEIGHT_SCALE = 0.816f * 0.92f;

    // Directional speed-scale coefficient: gait phase advances faster along the
    // isometric vertical axis (screen "up") to compensate for the 2:1 tile ratio.
    // Value ≈ sqrt(2)-1 ≈ 0.414 corrects the iso compression exactly.
    static constexpr float DIRECTIONAL_SPEED_SCALE = 0.41f;

    float directional_speed_scale = DIRECTIONAL_SPEED_SCALE; // dir multiplier coefficient for Y-axis iso correction
    float hip_sway_deg      = 5.0f;   // max lateral hip rotation (degrees)
    float hip_drop_max      = 0.03f;  // max fractional CoM drop during stride
    float hip_bob_amplitude = 0.02f;  // vertical double-bounce amplitude (world units)
};

// --- Phase 2: Look-at target ---
struct LookAtTarget {
    glm::vec3 position{0.0f};
    float weight = 0.0f;    // [0,1] blend toward target
    bool active = false;
};

// --- Phase 3: Arm IK goal ---
struct ArmIKGoal {
    glm::vec3 target_l{0.0f}, target_r{0.0f};
    float weight_l = 0.0f, weight_r = 0.0f;  // [0,1]: 0=pendulum FK, 1=IK
};

// --- Phase 4: Animation overlay ---
struct AnimationOverlay {
    enum class Type : uint8_t { None, Limp, Fatigue, HeavyCarry };
    Type active = Type::None;
    float intensity = 0.0f;  // [0,1]
    float phase = 0.0f;
};

// --- Phase 5: Grab state ---
struct GrabState {
    glm::vec3 grab_point{0.0f};
    float weight = 0.0f;
    bool active_l = false, active_r = false;
};

// Compute hip counter-animation state from stride phase and configuration.
// stride_phase: [0, 1) normalized gait phase
inline void compute_rig_hip_state(RigHipState &state,
                                   const AnimationConfig &cfg) {
    constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    float two_pi_phase = state.stride_phase * TWO_PI;

    state.hip_rotation_deg  = cfg.hip_sway_deg      * std::sin(two_pi_phase);
    state.hip_drop_fraction = cfg.hip_drop_max      * (1.0f - std::abs(std::cos(two_pi_phase)));
    state.hip_bob_y         = cfg.hip_bob_amplitude * std::abs(std::sin(two_pi_phase));
}
