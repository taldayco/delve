#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include "terrain/terrain_mesh.h"  // BasaltVertex
#include <vector>

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
    float neck_radius    = 0.015f;   // 0.05 * 0.3
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