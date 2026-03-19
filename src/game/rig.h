#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include "render/skeletal_animation.h"
#include <vector>
#include <string>
#include <algorithm>

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

// Legacy joint enum — retained for animation tests and telemetry
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

struct BoneMap {
    int hips = -1, spine = -1, spine1 = -1, chest = -1;
    int neck = -1, head = -1;
    int l_upper_leg = -1, l_lower_leg = -1, l_foot = -1, l_toe = -1;
    int r_upper_leg = -1, r_lower_leg = -1, r_foot = -1, r_toe = -1;
    int l_upper_arm = -1, l_lower_arm = -1, l_hand = -1;
    int r_upper_arm = -1, r_lower_arm = -1, r_hand = -1;
    int l_shoulder = -1, r_shoulder = -1;

    static BoneMap build_from_skeleton(const GltfSkeleton &skel) {
        BoneMap m;
        auto lower = [](const std::string &s) {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return out;
        };
        auto has = [](const std::string &s, const std::string &sub) {
            return s.find(sub) != std::string::npos;
        };
        auto is_left = [&](const std::string &s) {
            return has(s, "left") || has(s, ".l") || has(s, "_l") || has(s, "l_")
                || has(s, ":left");
        };
        auto is_right = [&](const std::string &s) {
            return has(s, "right") || has(s, ".r") || has(s, "_r") || has(s, "r_")
                || has(s, ":right");
        };

        for (int i = 0; i < (int)skel.bones.size(); ++i) {
            std::string name = lower(skel.bones[i].name);

            if (has(name, "hips") || has(name, "pelvis")) {
                m.hips = i;
            } else if ((has(name, "shoulder") || has(name, "clavicle"))) {
                if (is_left(name))  m.l_shoulder = i;
                else if (is_right(name)) m.r_shoulder = i;
            } else if (has(name, "spine")) {
                if (has(name, "1") || has(name, "spine1")) m.spine1 = i;
                else if (m.spine < 0) m.spine = i;
                else m.spine1 = i;
            } else if (has(name, "chest") || has(name, "spine2")) {
                m.chest = i;
            } else if (has(name, "neck")) {
                m.neck = i;
            } else if (has(name, "head")) {
                m.head = i;
            } else if (has(name, "upleg") || has(name, "thigh") || has(name, "upperleg") || has(name, "upper_leg")) {
                if (is_left(name))  m.l_upper_leg = i;
                else if (is_right(name)) m.r_upper_leg = i;
            } else if ((has(name, "leg") || has(name, "shin") || has(name, "lowerleg") || has(name, "lower_leg"))
                       && !has(name, "up")) {
                if (is_left(name))  m.l_lower_leg = i;
                else if (is_right(name)) m.r_lower_leg = i;
            } else if (has(name, "foot") && !has(name, "toe")) {
                if (is_left(name))  m.l_foot = i;
                else if (is_right(name)) m.r_foot = i;
            } else if (has(name, "toe")) {
                if (is_left(name))  m.l_toe = i;
                else if (is_right(name)) m.r_toe = i;
            } else if (has(name, "uparm") || has(name, "upperarm") || has(name, "upper_arm")) {
                if (is_left(name))  m.l_upper_arm = i;
                else if (is_right(name)) m.r_upper_arm = i;
            } else if (has(name, "forearm") || has(name, "lowerarm") || has(name, "lower_arm")) {
                if (is_left(name))  m.l_lower_arm = i;
                else if (is_right(name)) m.r_lower_arm = i;
            } else if (has(name, "hand") && !has(name, "thumb") && !has(name, "index") && !has(name, "middle") && !has(name, "ring") && !has(name, "pinky")) {
                if (is_left(name))  m.l_hand = i;
                else if (is_right(name)) m.r_hand = i;
            }
        }
        return m;
    }
};

struct SkinnedPose {
    std::vector<BoneLocalTransform> local_transforms;
};
