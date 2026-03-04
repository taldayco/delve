#include "render/skeleton.h"
#include <glm/glm.hpp>

SkeletonPose make_rest_pose() {
    // Use ActorConfig defaults to ensure proportions match the runtime skeleton.
    ActorConfig c;
    SkeletonPose pose;

    // Root / hips at origin
    pose.joints[(int)Joint::ROOT]       = glm::vec3(0.0f, 0.0f, 0.0f);

    // Spine (midpoint of torso)
    pose.joints[(int)Joint::SPINE]      = glm::vec3(0.0f, 0.0f, c.torso_len * 0.5f);

    // Chest, neck, head
    pose.joints[(int)Joint::CHEST]      = glm::vec3(0.0f, 0.0f, c.torso_len);
    pose.joints[(int)Joint::NECK]       = glm::vec3(0.0f, 0.0f, c.torso_len + c.neck_len);
    pose.joints[(int)Joint::HEAD]       = glm::vec3(0.0f, 0.0f, c.torso_len + c.neck_len + 2.0f * c.head_radius);

    // Arms — extend along ±x at chest height
    pose.joints[(int)Joint::L_SHOULDER] = glm::vec3( c.shoulder_width, 0.0f, c.torso_len);
    pose.joints[(int)Joint::L_ELBOW]    = glm::vec3( c.shoulder_width + c.arm_len, 0.0f, c.torso_len);
    pose.joints[(int)Joint::L_WRIST]    = glm::vec3( c.shoulder_width + c.arm_len + c.forearm_len, 0.0f, c.torso_len);

    pose.joints[(int)Joint::R_SHOULDER] = glm::vec3(-c.shoulder_width, 0.0f, c.torso_len);
    pose.joints[(int)Joint::R_ELBOW]    = glm::vec3(-c.shoulder_width - c.arm_len, 0.0f, c.torso_len);
    pose.joints[(int)Joint::R_WRIST]    = glm::vec3(-c.shoulder_width - c.arm_len - c.forearm_len, 0.0f, c.torso_len);

    // Legs — descend along -z from hip sockets at root level
    pose.joints[(int)Joint::L_HIP]      = glm::vec3( c.hip_width, 0.0f, 0.0f);
    pose.joints[(int)Joint::L_KNEE]     = glm::vec3( c.hip_width, 0.0f, -c.leg_len);
    pose.joints[(int)Joint::L_ANKLE]    = glm::vec3( c.hip_width, 0.0f, -(c.leg_len + c.shin_len));

    pose.joints[(int)Joint::R_HIP]      = glm::vec3(-c.hip_width, 0.0f, 0.0f);
    pose.joints[(int)Joint::R_KNEE]     = glm::vec3(-c.hip_width, 0.0f, -c.leg_len);
    pose.joints[(int)Joint::R_ANKLE]    = glm::vec3(-c.hip_width, 0.0f, -(c.leg_len + c.shin_len));

    return pose;
}

void apply_forward_kinematics(SkeletonPose &pose, const SkeletonPose &rest_pose) {
    // Joints are already placed in world space by the caller; nothing to do.
    (void)pose;
    (void)rest_pose;
}