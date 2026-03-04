#pragma once
#include "actor.h"
#include "config.h"

// Natural bone segment lengths derived from Config::ACTOR_*_WU constants.
// Total height from ankle(-z) to head crown(+z) ≈ Config::ACTOR_TOTAL_HEIGHT_WU.
// Coordinate system: x=right, y=forward, z=up (matches world space).
//
// Rest pose layout (ROOT at z=0, legs descend in -z):
//   - ROOT (pelvis)     at z=0
//   - SPINE             at z= TORSO*0.6*0.5
//   - CHEST             at z= TORSO*0.6
//   - NECK              at z= TORSO*0.6 + NECK_HEIGHT
//   - HEAD              at z= TORSO*0.6 + NECK_HEIGHT + HEAD_HEIGHT
//   - L/R_SHOULDER      at x=±SHOULDER_WIDTH/2, z=CHEST_z
//   - L/R_ELBOW         at x=±(SHOULDER/2 + UPPER_ARM), z=CHEST_z
//   - L/R_WRIST         at x=±(SHOULDER/2 + UPPER_ARM + FOREARM), z=CHEST_z
//   - L/R_HIP           at x=±HIP_WIDTH/2, z=0
//   - L/R_KNEE          at x=±HIP_WIDTH/2, z=-UPPER_LEG
//   - L/R_ANKLE         at x=±HIP_WIDTH/2, z=-(UPPER_LEG+LOWER_LEG)
//
// Total upward height (ROOT to HEAD) ≈ TORSO*0.6 + NECK + HEAD ≈ 3/8 * H
// Total downward depth (ROOT to ANKLE) = UPPER_LEG + LOWER_LEG = 1/2 * H
// Full span (ankle to head) ≈ ACTOR_TOTAL_HEIGHT_WU
struct BoneNaturalLengths {
    // Torso
    float chest_core    = Config::ACTOR_TORSO_HEIGHT_WU * 0.6f;
    float neck          = Config::ACTOR_NECK_HEIGHT_WU;
    float head          = Config::ACTOR_HEAD_HEIGHT_WU;
    // Arms (per side)
    float shoulder_conn = Config::ACTOR_SHOULDER_WIDTH_WU * 0.5f;
    float upper_arm     = Config::ACTOR_UPPER_ARM_WU;
    float forearm       = Config::ACTOR_FOREARM_WU;
    float hand          = Config::ACTOR_HAND_LEN_WU;
    // Legs (per side)
    float hip_conn      = Config::ACTOR_HIP_WIDTH_WU * 0.5f;
    float upper_leg     = Config::ACTOR_UPPER_LEG_WU;
    float lower_leg     = Config::ACTOR_LOWER_LEG_WU;
    float foot          = Config::ACTOR_FOOT_LEN_WU;
};

// Build a rest pose using Config::ACTOR_*_WU proportions.
// ROOT (pelvis) is at the origin; legs descend along -z; torso rises along +z.
// Total chain height from ankle to head crown ≈ ACTOR_TOTAL_HEIGHT_WU.
SkeletonPose make_rest_pose();

// Passthrough: joint positions computed by compute_walk_pose/compute_idle_pose are
// already in world space and require no FK propagation step.
void apply_forward_kinematics(SkeletonPose &pose, const SkeletonPose &rest_pose);