#include "render/animation.h"
#include "render/skeleton.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

// Walk animation amplitudes.
static constexpr float LEG_SWING  = 0.55f;  // hip swing angle (radians)
static constexpr float KNEE_BEND  = 0.65f;  // knee flexion added to swing angle (radians)
static constexpr float ANKLE_DORS = 0.20f;  // ankle dorsiflexion amplitude (radians)
static constexpr float ARM_SWING  = 0.08f;  // arm swing amplitude (world units lateral)

// Compute leg joint positions from hip socket using forward kinematics angles.
// swing:  angle of upper leg (rotation about Y — forward/back swing)
// bend:   extra knee flexion angle
// ankle:  ankle dorsiflexion angle (positive = toes up)
static void compute_leg_joints(const ActorConfig &c,
                                glm::vec3 hip_socket,
                                float swing, float bend, float ankle_df,
                                glm::vec3 &out_knee, glm::vec3 &out_ankle) {
    // Upper leg: rotate straight-down vector by swing angle about local Y (forward) axis.
    float ky = c.leg_len * sinf(swing);         // forward displacement
    float kz = -c.leg_len * cosf(swing);        // downward displacement
    out_knee = hip_socket + glm::vec3(0.0f, ky, kz);

    // Lower leg: total angle = swing + bend (knee bends in same plane as swing).
    float total = swing + bend;
    float ay = c.shin_len * sinf(total);
    float az = -c.shin_len * cosf(total);
    out_ankle = out_knee + glm::vec3(0.0f, ay, az);
}

SkeletonPose compute_walk_pose(const ActorConfig &c, float walk_phase, glm::vec3 hip) {
    SkeletonPose pose;

    // --- Upper body (direct placement from hip anchor) ---
    pose.joints[(int)Joint::ROOT]  = hip;
    pose.joints[(int)Joint::SPINE] = hip + glm::vec3(0.0f, 0.0f, c.torso_len * 0.5f);
    pose.joints[(int)Joint::CHEST] = hip + glm::vec3(0.0f, 0.0f, c.torso_len);
    pose.joints[(int)Joint::NECK]  = hip + glm::vec3(0.0f, 0.0f, c.torso_len + c.neck_len);
    pose.joints[(int)Joint::HEAD]  = hip + glm::vec3(0.0f, 0.0f, c.torso_len + c.neck_len + c.head_radius);

    // --- Shoulders ---
    glm::vec3 l_shoulder = hip + glm::vec3( c.shoulder_width, 0.0f, c.torso_len);
    glm::vec3 r_shoulder = hip + glm::vec3(-c.shoulder_width, 0.0f, c.torso_len);
    pose.joints[(int)Joint::L_SHOULDER] = l_shoulder;
    pose.joints[(int)Joint::R_SHOULDER] = r_shoulder;

    // --- Arm swing: counter-phase to opposite leg ---
    float l_arm = sinf(walk_phase + glm::half_pi<float>()) * ARM_SWING;
    float r_arm = sinf(walk_phase - glm::half_pi<float>()) * ARM_SWING;

    pose.joints[(int)Joint::L_ELBOW] = l_shoulder + glm::vec3(c.arm_len, l_arm, 0.0f);
    pose.joints[(int)Joint::L_WRIST] = pose.joints[(int)Joint::L_ELBOW]
                                       + glm::vec3(c.forearm_len, l_arm * 0.5f, 0.0f);
    pose.joints[(int)Joint::R_ELBOW] = r_shoulder + glm::vec3(-c.arm_len, r_arm, 0.0f);
    pose.joints[(int)Joint::R_WRIST] = pose.joints[(int)Joint::R_ELBOW]
                                       + glm::vec3(-c.forearm_len, r_arm * 0.5f, 0.0f);

    // --- Hip sockets ---
    glm::vec3 l_hip = hip + glm::vec3( c.hip_width, 0.0f, 0.0f);
    glm::vec3 r_hip = hip + glm::vec3(-c.hip_width, 0.0f, 0.0f);
    pose.joints[(int)Joint::L_HIP] = l_hip;
    pose.joints[(int)Joint::R_HIP] = r_hip;

    // --- Leg swing angles: left and right are anti-phase ---
    float l_swing_angle = sinf(walk_phase) * LEG_SWING;
    float r_swing_angle = sinf(walk_phase + glm::pi<float>()) * LEG_SWING;

    // Knee bend: always positive, peaks when the thigh is at neutral (no swing).
    float l_knee_angle = KNEE_BEND * (0.5f + 0.5f * cosf(walk_phase));
    float r_knee_angle = KNEE_BEND * (0.5f + 0.5f * cosf(walk_phase + glm::pi<float>()));

    // Ankle dorsiflexion: toes lift as leg swings forward (phase offset +π/2).
    float l_ankle_df = sinf(walk_phase + glm::half_pi<float>()) * ANKLE_DORS;
    float r_ankle_df = sinf(walk_phase + glm::pi<float>() + glm::half_pi<float>()) * ANKLE_DORS;

    glm::vec3 l_knee, l_ankle, r_knee, r_ankle;
    compute_leg_joints(c, l_hip, l_swing_angle, l_knee_angle, l_ankle_df, l_knee, l_ankle);
    compute_leg_joints(c, r_hip, r_swing_angle, r_knee_angle, r_ankle_df, r_knee, r_ankle);

    pose.joints[(int)Joint::L_KNEE]  = l_knee;
    pose.joints[(int)Joint::L_ANKLE] = l_ankle;
    pose.joints[(int)Joint::R_KNEE]  = r_knee;
    pose.joints[(int)Joint::R_ANKLE] = r_ankle;

    return pose;
}

SkeletonPose compute_idle_pose(const ActorConfig &c, float time, glm::vec3 hip) {
    SkeletonPose pose;

    // Breathing: slow chest rise/fall at 0.5 Hz.
    float breath = sinf(time * glm::two_pi<float>() * 0.5f) * 0.008f;

    // Idle weight-shift: slow lateral sway at 0.15 Hz.
    float shift = sinf(time * glm::two_pi<float>() * 0.15f) * 0.005f;

    pose.joints[(int)Joint::ROOT]  = hip;
    pose.joints[(int)Joint::SPINE] = hip + glm::vec3(0.0f, 0.0f, c.torso_len * 0.5f + breath * 0.3f);
    pose.joints[(int)Joint::CHEST] = hip + glm::vec3(0.0f, 0.0f, c.torso_len + breath);
    pose.joints[(int)Joint::NECK]  = hip + glm::vec3(0.0f, 0.0f, c.torso_len + c.neck_len + breath * 0.6f);
    pose.joints[(int)Joint::HEAD]  = hip + glm::vec3(0.0f, 0.0f, c.torso_len + c.neck_len + c.head_radius + breath * 0.4f);

    glm::vec3 chest_pos = pose.joints[(int)Joint::CHEST];
    pose.joints[(int)Joint::L_SHOULDER] = chest_pos + glm::vec3( c.shoulder_width, 0.0f, 0.0f);
    pose.joints[(int)Joint::R_SHOULDER] = chest_pos + glm::vec3(-c.shoulder_width, 0.0f, 0.0f);

    // Arms hang with a slight natural elbow bend.
    pose.joints[(int)Joint::L_ELBOW] = pose.joints[(int)Joint::L_SHOULDER]
                                       + glm::vec3( c.arm_len, 0.0f, -c.arm_len * 0.1f);
    pose.joints[(int)Joint::L_WRIST] = pose.joints[(int)Joint::L_ELBOW]
                                       + glm::vec3( c.forearm_len, 0.0f, -c.forearm_len * 0.15f);
    pose.joints[(int)Joint::R_ELBOW] = pose.joints[(int)Joint::R_SHOULDER]
                                       + glm::vec3(-c.arm_len, 0.0f, -c.arm_len * 0.1f);
    pose.joints[(int)Joint::R_WRIST] = pose.joints[(int)Joint::R_ELBOW]
                                       + glm::vec3(-c.forearm_len, 0.0f, -c.forearm_len * 0.15f);

    // Legs: standing straight with slight lateral weight shift.
    glm::vec3 l_hip = hip + glm::vec3( c.hip_width + shift, 0.0f, 0.0f);
    glm::vec3 r_hip = hip + glm::vec3(-c.hip_width - shift, 0.0f, 0.0f);
    pose.joints[(int)Joint::L_HIP]   = l_hip;
    pose.joints[(int)Joint::L_KNEE]  = l_hip + glm::vec3(0.0f, 0.0f, -c.leg_len);
    pose.joints[(int)Joint::L_ANKLE] = l_hip + glm::vec3(0.0f, 0.0f, -(c.leg_len + c.shin_len));
    pose.joints[(int)Joint::R_HIP]   = r_hip;
    pose.joints[(int)Joint::R_KNEE]  = r_hip + glm::vec3(0.0f, 0.0f, -c.leg_len);
    pose.joints[(int)Joint::R_ANKLE] = r_hip + glm::vec3(0.0f, 0.0f, -(c.leg_len + c.shin_len));

    return pose;
}
