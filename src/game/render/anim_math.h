#pragma once
#include "skeletal_animation.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

float smooth_damp(float current, float target, float *velocity,
                  float smooth_time, float dt);

float smooth_damp_angle(float current, float target, float *velocity,
                        float smooth_time, float dt);

void additive_rotation(BoneLocalTransform &base, const glm::quat &delta, float weight);

void additive_translation(BoneLocalTransform &base, const glm::vec3 &offset, float weight);

struct StepTiming {
    float adaptive_duration;
    float speed_factor;
};

StepTiming gait_step_timing(float speed, float move_speed, float step_duration,
                            float turn_urgency);

float gait_trigger_distance(float half_stride, float speed_factor);

glm::vec3 gait_foot_arc(const glm::vec3 &from, const glm::vec3 &to,
                        float progress, float step_height);
