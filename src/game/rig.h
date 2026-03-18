#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include "terrain/terrain_mesh.h"
#include <vector>

struct Player  {};
struct ActorTag {};

struct Transform {
    float x = 0, y = 0, z = 0;
    float facing = 0;
};

struct Velocity {
    float x = 0, y = 0, z = 0;
};

struct ActorConfig {
    float hip_width      = 0.25f;
    float shoulder_width = 0.35f;
    float leg_len        = 0.366f;
    float shin_len       = 0.332f;
    float torso_len      = 0.520f;
    float neck_len       = 0.104f;
    float head_radius    = 0.042f;
    float arm_len        = 0.270f;
    float forearm_len    = 0.230f;
    float leg_radius     = 0.021f;
    float arm_radius     = 0.017f;
    float torso_radius   = 0.027f;
    float neck_radius    = 0.015f;
    float toe_len        = 0.07f;
};

struct ProceduralGait {
    float phase         = 0.0f;
    float phase_target  = 0.0f;
    float phase_rate    = 0.0f;
    float stride_len    = 0.85f;
    float step_height   = 0.16f;
    float step_duration = 0.25f;
    float move_speed    = 4.0f;
};

enum class Joint : uint8_t {
    ROOT = 0,
    HIPS,
    SPINE_01,
    SPINE_02,
    CHEST,
    NECK,
    HEAD,
    HEAD_END,

    L_CLAVICLE,
    L_UPPER_ARM,
    L_LOWER_ARM,
    L_HAND,

    R_CLAVICLE,
    R_UPPER_ARM,
    R_LOWER_ARM,
    R_HAND,

    L_UPPER_LEG,
    L_LOWER_LEG,
    L_FOOT,
    L_TOE,

    R_UPPER_LEG,
    R_LOWER_LEG,
    R_FOOT,
    R_TOE,

    POLE_KNEE_L,
    POLE_KNEE_R,
    POLE_ELBOW_L,
    POLE_ELBOW_R,
    IK_FOOT_L,
    IK_FOOT_R,
    IK_HAND_L,
    IK_HAND_R,

    COUNT
};

struct RigPose {
    glm::vec3 joints[(int)Joint::COUNT] = {};
};

struct RigTransforms {
    glm::mat4 bones[(int)Joint::COUNT] = {};
};

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

inline void build_bone_basis(const glm::vec3 &bone_dir,
                              const glm::vec3 &ref_fwd,
                              glm::vec3 &out_right,
                              glm::vec3 &out_fwd,
                              glm::vec3 &out_up) {
    float len = glm::length(bone_dir);
    if (len < 1e-5f) {
        out_right = glm::vec3(1.0f, 0.0f, 0.0f);
        out_fwd   = glm::vec3(0.0f, 1.0f, 0.0f);
        out_up    = glm::vec3(0.0f, 0.0f, 1.0f);
        return;
    }
    out_up = bone_dir / len;

    out_right = glm::cross(ref_fwd, out_up);
    float right_len = glm::length(out_right);
    if (right_len < 1e-5f) {
        glm::vec3 alt = (std::abs(out_up.z) < 0.9f)
                            ? glm::vec3(0.0f, 0.0f, 1.0f)
                            : glm::vec3(1.0f, 0.0f, 0.0f);
        out_right = glm::normalize(glm::cross(alt, out_up));
    } else {
        out_right /= right_len;
    }

    out_fwd = glm::cross(out_up, out_right);
}

struct ProceduralMesh {
    std::vector<BasaltVertex> vertices;
};

struct LegState {
    glm::vec3 foot[2]      = {};
    glm::vec3 prev_foot[2] = {};
    glm::vec3 target[2]    = {};
    float     progress[2]  = {};
    bool      stepping[2]  = {};

    glm::vec3 plant_pos[2] = {};
    bool      planted[2]   = {};

    int       turn_step_queued = -1;

    int       last_step_leg = -1;
};

struct RigState {
    glm::vec3 smooth_velocity{0.0f};
    glm::vec3 velocity_rate{0.0f};

    float support_balance      = 0.0f;
    float support_balance_rate = 0.0f;

    float lean_x = 0.0f;
    float lean_y = 0.0f;
    float chest_lean_x_rate = 0.0f;
    float chest_lean_y_rate = 0.0f;
    float neck_lean_x_rate  = 0.0f;
    float neck_lean_y_rate  = 0.0f;
    float head_lean_x_rate  = 0.0f;
    float head_lean_y_rate  = 0.0f;
    float chest_lean_x = 0.0f;
    float chest_lean_y = 0.0f;
    float neck_lean_x  = 0.0f;
    float neck_lean_y  = 0.0f;
    float head_lean_x  = 0.0f;
    float head_lean_y  = 0.0f;

    float l_arm_target      = 0.0f;
    float r_arm_target      = 0.0f;
    float l_shoulder_smooth = 0.0f;
    float l_elbow_smooth    = 0.0f;
    float l_wrist_smooth    = 0.0f;
    float r_shoulder_smooth = 0.0f;
    float r_elbow_smooth    = 0.0f;
    float r_wrist_smooth    = 0.0f;
    float l_shoulder_rate   = 0.0f;
    float l_elbow_rate      = 0.0f;
    float l_wrist_rate      = 0.0f;
    float r_shoulder_rate   = 0.0f;
    float r_elbow_rate      = 0.0f;
    float r_wrist_rate      = 0.0f;

    float breath_phase    = 0.0f;
    float idle_sway_phase = 0.0f;
    float idle_weight_phase = 0.0f;

    float foot_contact_velocity[2] = {0.0f, 0.0f};

    glm::vec3 prev_velocity{0.0f};

    float hip_roll      = 0.0f;
    float hip_roll_rate = 0.0f;
    float hip_bob       = 0.0f;
    float hip_bob_rate  = 0.0f;

    float hip_dip      = 0.0f;
    float hip_dip_rate = 0.0f;

    float hip_tilt      = 0.0f;
    float hip_tilt_rate = 0.0f;

    float look_yaw        = 0.0f;
    float look_pitch      = 0.0f;
    float look_yaw_rate   = 0.0f;
    float look_pitch_rate = 0.0f;

    float visual_facing      = 0.0f;
    float visual_facing_rate = 0.0f;

    float turn_delta       = 0.0f;
    float turn_magnitude   = 0.0f;
    float turn_urgency     = 0.0f;
    bool  in_large_turn    = false;

    float chest_facing      = 0.0f;
    float chest_facing_rate = 0.0f;
};

struct RigHipState {
    float stride_phase      = 0.0f;
    float hip_rotation_deg  = 0.0f;
    float hip_drop_fraction = 0.0f;
    float hip_bob_y         = 0.0f;
};

struct AnimationConfig {
    static constexpr float ISO_CHAR_HEIGHT_SCALE = 0.816f * 0.92f;

    static constexpr float DIRECTIONAL_SPEED_SCALE = 0.41f;

    float directional_speed_scale = DIRECTIONAL_SPEED_SCALE;
    float hip_sway_deg      = 5.0f;
    float hip_drop_max      = 0.03f;
    float hip_bob_amplitude = 0.02f;
};

struct LookAtTarget {
    glm::vec3 position{0.0f};
    float weight = 0.0f;
    bool active = false;
};

struct ArmIKGoal {
    glm::vec3 target_l{0.0f}, target_r{0.0f};
    float weight_l = 0.0f, weight_r = 0.0f;
};

struct AnimationOverlay {
    enum class Type : uint8_t { None, Limp, Fatigue, HeavyCarry };
    Type active = Type::None;
    float intensity = 0.0f;
    float phase = 0.0f;
};

struct GrabState {
    glm::vec3 grab_point{0.0f};
    float weight = 0.0f;
    bool active_l = false, active_r = false;
};

inline void compute_rig_hip_state(RigHipState &state,
                                   const AnimationConfig &cfg) {
    constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    float two_pi_phase = state.stride_phase * TWO_PI;

    state.hip_rotation_deg  = cfg.hip_sway_deg      * std::sin(two_pi_phase);
    state.hip_drop_fraction = cfg.hip_drop_max      * (1.0f - std::abs(std::cos(two_pi_phase)));
    state.hip_bob_y         = cfg.hip_bob_amplitude * std::abs(std::sin(two_pi_phase));
}
