#include "actor_animation.h"
#include "../topo_game.h"
#include "../animation_log.h"
#include "../../engine/input/input.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <mutex>

// Critically-damped spring smoothing (Game Programming Gems 4).
static float smooth_damp(float current, float target, float& vel,
                          float smooth_time, float dt)
{
    smooth_time = std::max(smooth_time, 0.0001f);
    float omega = 2.f / smooth_time;
    float x     = omega * dt;
    float exp_x = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
    float delta = current - target;
    float temp  = (vel + omega * delta) * dt;
    vel         = (vel - omega * temp) * exp_x;
    return target + (delta + temp) * exp_x;
}

void register_animation_systems(flecs::world& world,
                                  AsyncTerrainState* async_state)
{
    // 1. PlayerMovementSystem
    world.system<Transform, Velocity, ProceduralGait, AnimationState>(
        "PlayerMovementSystem")
        .with<Player>()
        .each([](Transform& t, Velocity& vel, ProceduralGait& gait,
                 AnimationState& anim) {
            const float spd = 3.5f;
            const float dt  = 1.f / 60.f;
            float dx = 0.f, dy = 0.f;
            if (Input::is_key_held(SDL_SCANCODE_W) || Input::is_key_held(SDL_SCANCODE_UP))    dy += 1.f;
            if (Input::is_key_held(SDL_SCANCODE_S) || Input::is_key_held(SDL_SCANCODE_DOWN))  dy -= 1.f;
            if (Input::is_key_held(SDL_SCANCODE_A) || Input::is_key_held(SDL_SCANCODE_LEFT))  dx -= 1.f;
            if (Input::is_key_held(SDL_SCANCODE_D) || Input::is_key_held(SDL_SCANCODE_RIGHT)) dx += 1.f;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len > 0.f) { dx /= len; dy /= len; }

            float raw_vx = dx * spd;
            float raw_vy = dy * spd;
            vel.x = raw_vx;
            vel.y = raw_vy;

            const float smooth_t = 0.12f;
            anim.smooth_vel.x = smooth_damp(anim.smooth_vel.x, raw_vx,
                                             anim.vel_vel.x, smooth_t, dt);
            anim.smooth_vel.y = smooth_damp(anim.smooth_vel.y, raw_vy,
                                             anim.vel_vel.y, smooth_t, dt);

            t.x += vel.x * dt;
            t.y += vel.y * dt;
            if (len > 0.f) t.facing = std::atan2(dy, dx);
            gait.move_speed = len * spd;

            // Speed-adaptive step duration
            const float dur_slow = 0.45f;
            const float dur_fast = 0.22f;
            float t_norm = glm::clamp(gait.move_speed / spd, 0.f, 1.f);
            gait.step_duration = glm::mix(dur_slow, dur_fast, t_norm);
        });

    // 2. ActorGroundingSystem
    auto map_ptr = async_state;
    world.system<Transform>("ActorGroundingSystem")
        .each([map_ptr](Transform& t) {
            std::lock_guard<std::mutex> lk(map_ptr->mtx);
            if (!map_ptr->ready) return;
            const MapData& md = map_ptr->data;
            auto [cx, cy] = md.world_to_grid(t.x, t.y);
            if (cx < 0 || cy < 0 || cx >= md.width || cy >= md.height) return;
            float h     = md.elevation[cy * md.width + cx];
            float dist  = std::abs(h - t.z);
            float alpha = dist > 0.3f ? 0.25f : 0.15f;
            t.z += (h - t.z) * alpha;
        });

    // 3. GaitSystem
    world.system<Transform, Velocity, ProceduralGait, LegState, ActorConfig>(
        "GaitSystem")
        .each([](Transform& t, Velocity& vel,
                 ProceduralGait& gait, LegState& legs, ActorConfig& cfg) {
            const float dt     = 1.f / 60.f;
            const float speed  = gait.move_speed;
            const float stride = gait.stride_len;
            if (speed < 0.05f) return;

            gait.phase = std::fmod(
                gait.phase + (speed / stride) * dt, 1.f);

            glm::vec2 fwd  { std::cos(t.facing), std::sin(t.facing) };
            glm::vec2 right{ -fwd.y, fwd.x };

            for (int leg = 0; leg < 2; ++leg) {
                float side = (leg == 0) ? 1.f : -1.f;
                glm::vec2 hip_off  = right * (side * cfg.hip_width);
                glm::vec2 step_off = fwd * (stride * 0.5f);
                glm::vec2 ideal_foot{
                    t.x + hip_off.x + step_off.x,
                    t.y + hip_off.y + step_off.y
                };

                float dist = glm::length(ideal_foot - glm::vec2(legs.foot[leg]));
                if (!legs.stepping[leg] && dist > stride * 0.55f) {
                    if (!legs.stepping[1 - leg]) {
                        legs.stepping[leg]  = true;
                        legs.progress[leg]  = 0.f;
                        legs.prev_foot[leg] = legs.foot[leg];
                        legs.target[leg]    = glm::vec3(ideal_foot, t.z);
                    }
                }

                if (legs.stepping[leg]) {
                    legs.progress[leg] += dt / gait.step_duration;
                    if (legs.progress[leg] >= 1.f) {
                        legs.progress[leg] = 1.f;
                        legs.stepping[leg] = false;
                        legs.foot[leg]     = legs.target[leg];
                    }
                    float p        = legs.progress[leg];
                    float smooth_p = p * p * (3.f - 2.f * p);
                    float arc      = std::sin(smooth_p * glm::pi<float>()) * gait.step_height;
                    legs.foot[leg] = glm::mix(legs.prev_foot[leg],
                                              legs.target[leg], smooth_p);
                    legs.foot[leg].z += arc;
                }
            }
        });

    // 4. IKSystem
    world.system<Transform, Velocity, LegState, ActorConfig, SkeletonPose,
                 AnimationState, ProceduralGait>("IKSystem")
        .each([](Transform& t, Velocity& vel,
                 LegState& legs, ActorConfig& cfg,
                 SkeletonPose& pose, AnimationState& anim,
                 ProceduralGait& gait) {
            const float dt = 1.f / 60.f;

            pose.joints[J_HIP] = { t.x, t.y, t.z + cfg.leg_len + cfg.shin_len };

            glm::vec3 fwd3  { std::cos(t.facing), std::sin(t.facing), 0.f };
            glm::vec3 right3{ -fwd3.y, fwd3.x, 0.f };

            auto two_bone_ik = [&](glm::vec3 root, glm::vec3 foot_target,
                                    float upper, float lower,
                                    glm::vec3 pole,
                                    glm::vec3& knee_out) {
                glm::vec3 d    = foot_target - root;
                float     dist = glm::length(d);
                dist = glm::clamp(dist, std::abs(upper - lower),
                                  upper + lower - 0.001f);
                float cos_a = (upper * upper + dist * dist - lower * lower)
                            / (2.f * upper * dist);
                cos_a = glm::clamp(cos_a, -1.f, 1.f);
                float angle = std::acos(cos_a);
                glm::vec3 dn   = d / dist;
                glm::vec3 perp = glm::normalize(
                    pole - root - glm::dot(pole - root, dn) * dn);
                knee_out = root + dn * (upper * cos_a)
                               + perp * (upper * std::sin(angle));
            };

            for (int leg = 0; leg < 2; ++leg) {
                float side = (leg == 0) ? 1.f : -1.f;
                glm::vec3 hip_j{
                    pose.joints[J_HIP].x + right3.x * side * cfg.hip_width,
                    pose.joints[J_HIP].y + right3.y * side * cfg.hip_width,
                    pose.joints[J_HIP].z };
                glm::vec3 pole = hip_j + fwd3 * 0.5f;
                glm::vec3 knee;
                two_bone_ik(hip_j, legs.foot[leg],
                            cfg.leg_len, cfg.shin_len, pole, knee);
                if (leg == 0) {
                    pose.joints[J_HIP_L]   = hip_j;
                    pose.joints[J_KNEE_L]  = knee;
                    pose.joints[J_ANKLE_L] = legs.foot[leg];
                } else {
                    pose.joints[J_HIP_R]   = hip_j;
                    pose.joints[J_KNEE_R]  = knee;
                    pose.joints[J_ANKLE_R] = legs.foot[leg];
                }
            }

            // Spine chain
            glm::vec3 spine_base = pose.joints[J_HIP];
            float seg = cfg.torso_len / 3.f;
            for (int i = 0; i < 3; ++i)
                pose.joints[J_SPINE1 + i] = spine_base + glm::vec3{0.f, 0.f, seg * (i + 1)};

            glm::vec3 top = pose.joints[J_SPINE3];
            pose.joints[J_NECK] = top + glm::vec3{0.f, 0.f, cfg.neck_len};
            pose.joints[J_HEAD] = top + glm::vec3{0.f, 0.f, cfg.neck_len + cfg.head_radius};

            // Pendulum arm swing with joint delay chain
            float phase_rate = (gait.move_speed > 0.1f)
                ? gait.move_speed / gait.stride_len
                : 0.5f;

            anim.arm_phase[0] = std::fmod(anim.arm_phase[0] + phase_rate * dt, 1.f);
            anim.arm_phase[1] = std::fmod(anim.arm_phase[1] + phase_rate * dt, 1.f);

            // Elbow delay: separate SmoothDamp state for each arm
            // We use dedicated storage (not vel_vel which is for velocity smoothing)
            const float arm_delay_t = 0.15f;
            {
                float dummy0 = 0.f, dummy1 = 0.f;
                anim.arm_delay[0] = smooth_damp(anim.arm_delay[0], anim.arm_phase[0],
                                                 dummy0, arm_delay_t, dt);
                anim.arm_delay[1] = smooth_damp(anim.arm_delay[1], anim.arm_phase[1],
                                                 dummy1, arm_delay_t, dt);
            }

            glm::vec3 shoulder_base = pose.joints[J_SPINE3];
            float swing_amp = (gait.move_speed > 0.1f)
                ? glm::clamp(gait.move_speed * 0.08f, 0.05f, 0.18f)
                : 0.02f;

            for (int arm = 0; arm < 2; ++arm) {
                float side        = (arm == 0) ? 1.f : -1.f;
                float phase       = anim.arm_phase[arm];
                float delay_phase = anim.arm_delay[arm];

                float elbow_swing = std::sin(delay_phase * glm::two_pi<float>()) * swing_amp;
                float elbow_bend  = std::max(0.f, std::sin(phase * glm::pi<float>())) * 0.06f;

                glm::vec3 shoulder{
                    shoulder_base.x + right3.x * side * cfg.shoulder_width,
                    shoulder_base.y + right3.y * side * cfg.shoulder_width,
                    shoulder_base.z };

                glm::vec3 elbow = shoulder
                    + fwd3 * elbow_swing
                    + glm::vec3{0.f, 0.f, -cfg.arm_len + elbow_bend};

                if (arm == 0) {
                    pose.joints[J_SHOULDER_L] = shoulder;
                    pose.joints[J_ELBOW_L]    = elbow;
                } else {
                    pose.joints[J_SHOULDER_R] = shoulder;
                    pose.joints[J_ELBOW_R]    = elbow;
                }
            }
        });

    // 5. SkeletonFinaliseSystem
    world.system<Transform, Velocity, SkeletonPose, ProceduralGait,
                 ActorConfig, AnimationState>("SkeletonFinaliseSystem")
        .each([](Transform& t, Velocity& vel, SkeletonPose& pose,
                 ProceduralGait& gait, ActorConfig& cfg,
                 AnimationState& anim) {
            const float dt = 1.f / 60.f;

            // Hip sway
            if (gait.move_speed > 0.1f)
                anim.sway_phase += dt * gait.move_speed * 6.f;
            anim.sway_amt = glm::mix(anim.sway_amt,
                gait.move_speed > 0.1f ? 0.04f : 0.f, 0.08f);
            float sway = std::sin(anim.sway_phase) * anim.sway_amt;
            pose.joints[J_HIP].x += sway;

            // Acceleration-driven torso lean
            glm::vec2 accel = anim.smooth_vel - anim.prev_smooth_vel;
            anim.prev_smooth_vel = anim.smooth_vel;

            float target_lean_x = -accel.x * 8.f - anim.smooth_vel.x * 0.015f;
            float target_lean_y = -accel.y * 8.f - anim.smooth_vel.y * 0.015f;

            anim.lean_x = glm::mix(anim.lean_x, target_lean_x, 0.12f);
            anim.lean_y = glm::mix(anim.lean_y, target_lean_y, 0.12f);

            // Successive breaking through spine chain
            const float spine_weights[3] = { 0.30f, 0.62f, 1.00f };
            for (int s = 0; s < 3; ++s) {
                pose.joints[J_SPINE1 + s].x += anim.lean_x * spine_weights[s];
                pose.joints[J_SPINE1 + s].y += anim.lean_y * spine_weights[s];
            }
            pose.joints[J_NECK].x += anim.lean_x;
            pose.joints[J_NECK].y += anim.lean_y;
            pose.joints[J_HEAD].x += anim.lean_x;
            pose.joints[J_HEAD].y += anim.lean_y;

            // Idle breathing micro-motion
            float idle_factor = glm::clamp(1.f - gait.move_speed * 0.8f, 0.f, 1.f);
            anim.breath_phase = std::fmod(anim.breath_phase + dt * 0.25f, 1.f);
            const float breath_amp = 0.012f;
            float breath = std::sin(anim.breath_phase * glm::two_pi<float>())
                         * breath_amp * idle_factor;
            for (int s = 0; s < 3; ++s)
                pose.joints[J_SPINE1 + s].z += breath * (s + 1) * 0.33f;
            pose.joints[J_NECK].z += breath;
            pose.joints[J_HEAD].z += breath;

            // Idle weight-shift micro-motion
            anim.weight_phase = std::fmod(anim.weight_phase + dt * 0.18f, 1.f);
            const float weight_amp = 0.008f;
            float weight_shift = std::sin(anim.weight_phase * glm::two_pi<float>())
                               * weight_amp * idle_factor;
            pose.joints[J_HIP].x += weight_shift;
        });

    // 6. AnimationLogSystem
    world.system<Transform, Velocity, ProceduralGait, LegState, SkeletonPose,
                 ActorConfig, AnimationState>("AnimationLogSystem")
        .each([](Transform& t, Velocity& vel, ProceduralGait& gait,
                 LegState& legs, SkeletonPose& pose, ActorConfig& cfg,
                 AnimationState& anim) {
            anim_log.log_transform(t, vel);
            anim_log.log_gait(gait);
            anim_log.log_legs(legs, t, cfg);
            anim_log.log_joints(pose, t);
            anim_log.log_finalize(anim.sway_phase, anim.sway_amt,
                                   anim.lean_x, anim.lean_y);
            anim_log.log_dynamics(anim.smooth_vel, anim.lean_x, anim.lean_y);
            anim_log.log_arm_swing(anim.arm_phase[0], anim.arm_phase[1],
                                    anim.arm_delay[0], anim.arm_delay[1]);
            anim_log.log_grounding(legs, gait.step_duration);
        });
}
