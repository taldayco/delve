#pragma once
#include "actor.h"

// Natural bone segment lengths matching ActorConfig defaults (H=2.0 world units).
// Total height: ankle(z=-0.92) to head crown(z≈1.06) ≈ 2.0 units.
// Upper leg and lower leg are equal (0.46 each) for correct anatomy.
// Arm reach: shoulder(0.28) + arm(0.30) + forearm(0.22) = 0.80 per side.
// NOTE: This struct is scaffolding for future use; not yet wired into the runtime.
struct BoneNaturalLengths {
    // Torso
    float spine      = 0.19f;  // ROOT → SPINE (torso_len * 0.5 = 0.19)
    float chest_core = 0.19f;  // SPINE → CHEST (torso_len * 0.5 = 0.19)
    float neck       = 0.30f;  // CHEST → NECK
    float head       = 0.36f;  // NECK → HEAD (2 * head_radius = 0.36)
    // Arms (per side)
    float shoulder_conn = 0.28f; // CHEST → SHOULDER
    float upper_arm     = 0.30f; // SHOULDER → ELBOW
    float forearm       = 0.22f; // ELBOW → WRIST
    // Legs (per side)
    float hip_conn  = 0.20f;  // SPINE → HIP (hip_width offset)
    float upper_leg = 0.46f;  // HIP → KNEE
    float lower_leg = 0.46f;  // KNEE → ANKLE (equal to upper_leg)
};

// Build a rest pose using ActorConfig default proportions (H=2.0 world units).
// Coordinate system: x=right, y=forward, z=up (matches world space).
// - ROOT (hips)      at z=0
// - SPINE (waist)    at z=0.19
// - CHEST            at z=0.38
// - NECK             at z=0.68
// - HEAD             at z=1.04  (neck + 2*head_radius = 0.68+0.36)
// - L/R_SHOULDER     at x=±0.28, z=0.38
// - L/R_ELBOW        at x=±0.58, z=0.38
// - L/R_WRIST        at x=±0.80, z=0.38
// - L/R_HIP          at x=±0.20, z=0
// - L/R_KNEE         at x=±0.20, z=-0.46
// - L/R_ANKLE        at x=±0.20, z=-0.92
SkeletonPose make_rest_pose();

// Passthrough: joint positions computed by compute_walk_pose/compute_idle_pose are
// already in world space and require no FK propagation step.
void apply_forward_kinematics(SkeletonPose &pose, const SkeletonPose &rest_pose);
