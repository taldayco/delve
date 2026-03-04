#include "render/skeleton.h"
#include "config.h"
#include <glm/glm.hpp>

SkeletonPose make_rest_pose() {
    SkeletonPose pose;

    // Derived constants from Config ACTOR_*_WU
    const float upper_leg     = Config::ACTOR_UPPER_LEG_WU;
    const float lower_leg     = Config::ACTOR_LOWER_LEG_WU;
    const float torso         = Config::ACTOR_TORSO_HEIGHT_WU;
    const float neck          = Config::ACTOR_NECK_HEIGHT_WU;
    const float head_h        = Config::ACTOR_HEAD_HEIGHT_WU;
    const float shoulder_half = Config::ACTOR_SHOULDER_WIDTH_WU * 0.5f;
    const float hip_half      = Config::ACTOR_HIP_WIDTH_WU * 0.5f;
    const float upper_arm     = Config::ACTOR_UPPER_ARM_WU;
    const float forearm       = Config::ACTOR_FOREARM_WU;

    // Torso heights above ROOT (pelvis)
    const float chest_core    = torso * 0.6f;   // CHEST bone = 60% of torso
    const float spine_z       = chest_core * 0.5f;
    const float chest_z       = chest_core;
    const float neck_z        = chest_core + neck;
    const float head_z        = neck_z + head_h;

    // Root / hips at origin (ground anchor; legs descend in -z)
    pose.joints[(int)Joint::ROOT]       = glm::vec3(0.0f, 0.0f, 0.0f);

    // Spine (midpoint of chest segment)
    pose.joints[(int)Joint::SPINE]      = glm::vec3(0.0f, 0.0f, spine_z);

    // Chest, neck, head
    pose.joints[(int)Joint::CHEST]      = glm::vec3(0.0f, 0.0f, chest_z);
    pose.joints[(int)Joint::NECK]       = glm::vec3(0.0f, 0.0f, neck_z);
    pose.joints[(int)Joint::HEAD]       = glm::vec3(0.0f, 0.0f, head_z);

    // Arms — extend along ±x at chest height
    pose.joints[(int)Joint::L_SHOULDER] = glm::vec3( shoulder_half, 0.0f, chest_z);
    pose.joints[(int)Joint::L_ELBOW]    = glm::vec3( shoulder_half + upper_arm, 0.0f, chest_z);
    pose.joints[(int)Joint::L_WRIST]    = glm::vec3( shoulder_half + upper_arm + forearm, 0.0f, chest_z);

    pose.joints[(int)Joint::R_SHOULDER] = glm::vec3(-shoulder_half, 0.0f, chest_z);
    pose.joints[(int)Joint::R_ELBOW]    = glm::vec3(-shoulder_half - upper_arm, 0.0f, chest_z);
    pose.joints[(int)Joint::R_WRIST]    = glm::vec3(-shoulder_half - upper_arm - forearm, 0.0f, chest_z);

    // Legs — descend along -z from hip sockets at root level
    pose.joints[(int)Joint::L_HIP]      = glm::vec3( hip_half, 0.0f, 0.0f);
    pose.joints[(int)Joint::L_KNEE]     = glm::vec3( hip_half, 0.0f, -upper_leg);
    pose.joints[(int)Joint::L_ANKLE]    = glm::vec3( hip_half, 0.0f, -(upper_leg + lower_leg));

    pose.joints[(int)Joint::R_HIP]      = glm::vec3(-hip_half, 0.0f, 0.0f);
    pose.joints[(int)Joint::R_KNEE]     = glm::vec3(-hip_half, 0.0f, -upper_leg);
    pose.joints[(int)Joint::R_ANKLE]    = glm::vec3(-hip_half, 0.0f, -(upper_leg + lower_leg));

    return pose;
}

void apply_forward_kinematics(SkeletonPose &pose, const SkeletonPose &rest_pose) {
    // Joints are already placed in world space by the caller; nothing to do.
    (void)pose;
    (void)rest_pose;
}