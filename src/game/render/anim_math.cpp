#include "render/anim_math.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

float smooth_damp(float current, float target, float *velocity,
                  float smooth_time, float dt) {
    float omega = 2.0f / smooth_time;
    float x     = omega * dt;
    float exp_f = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float delta = current - target;
    float temp  = (*velocity + omega * delta) * dt;
    *velocity   = (*velocity - omega * temp) * exp_f;
    return target + (delta + temp) * exp_f;
}

float smooth_damp_angle(float current, float target, float *velocity,
                        float smooth_time, float dt) {
    float delta = target - current;
    while (delta >  glm::pi<float>()) delta -= glm::two_pi<float>();
    while (delta < -glm::pi<float>()) delta += glm::two_pi<float>();
    return smooth_damp(current, current + delta, velocity, smooth_time, dt);
}

void additive_rotation(BoneLocalTransform &base, const glm::quat &delta, float weight) {
    glm::quat weighted = glm::slerp(glm::quat(1.f, 0.f, 0.f, 0.f), delta, weight);
    base.rotation = weighted * base.rotation;
}

void additive_translation(BoneLocalTransform &base, const glm::vec3 &offset, float weight) {
    base.translation += offset * weight;
}

StepTiming gait_step_timing(float speed, float move_speed, float step_duration,
                            float turn_urgency) {
    StepTiming t;
    float speed_ratio   = std::max(0.4f, std::min(1.0f, speed / move_speed));
    t.adaptive_duration = (step_duration / speed_ratio) * (1.0f - turn_urgency * 0.3f);
    t.speed_factor      = std::min(1.0f, speed / (move_speed * 0.15f))
                          * (1.0f - turn_urgency * 0.5f);
    return t;
}

float gait_trigger_distance(float half_stride, float speed_factor) {
    return half_stride * (0.25f + 0.75f * speed_factor);
}

glm::vec3 gait_foot_arc(const glm::vec3 &from, const glm::vec3 &to,
                        float progress, float step_height) {
    float p = std::clamp(progress, 0.0f, 1.0f);
    float warped_t = p * p * p * (p * (p * 6.0f - 15.0f) + 10.0f);
    float ts_z     = p * p * (3.0f - 2.0f * p);

    glm::vec3 out;
    out.x = from.x + (to.x - from.x) * warped_t;
    out.y = from.y + (to.y - from.y) * warped_t;
    out.z = from.z + (to.z - from.z) * ts_z
          + sinf(p * glm::pi<float>()) * step_height;
    return out;
}
