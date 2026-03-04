#include "actor_animation.h"
#include "actor.h"
#include "animation_log.h"
#include "game_state.h"
#include "input/input.h"
#include "terrain/map_util.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Critically damped spring SmoothDamp (Game Programming Gems 4 approximation)
// current: current value, target: target value
// vel: velocity state modified in-place, smoothing_time: spring decay seconds
// dt: delta time. Returns new value.
// ---------------------------------------------------------------------------
static float smooth_damp(float current, float target, float &vel,
                          float smoothing_time, float dt) {
    float omega   = 2.0f / smoothing_time;
    float x       = omega * dt;
    float exp_val = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change  = current - target;
    float temp    = (vel + omega * change) * dt;
    vel           = (vel - omega * temp) * exp_val;
    return target + (change + temp) * exp_val;
}

void register_animation_systems(
    flecs::world    &ecs,
    flecs::entity    player_entity,
    InputSystem     &input,
    AnimationLogger &anim_log)
{
    // -----------------------------------------------------------------------
    // 1. PlayerMovementSystem
    // -----------------------------------------------------------------------
    ecs.system("PlayerMovementSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, player_entity, &input](flecs::iter &) {
            auto *phase = ecs.get<GamePhase>();
            if (!phase || phase->current != GamePhase::Playing) return;
            if (!player_entity.is_alive()) return;

            auto *t    = player_entity.get_mut<Transform>();
            auto *vel  = player_entity.get_mut<Velocity>();
            auto *gait = player_entity.get_mut<ProceduralGait>();
            auto *anim = player_entity.get_mut<AnimationState>();
            if (!t || !vel || !gait || !anim) return;

            auto &in = input.state();
            float dt = ecs.delta_time();

            vel->x = 0.0f;
            vel->y = 0.0f;
            if (in.held[(int)Action::MoveUp])    vel->y -= gait->move_speed;
            if (in.held[(int)Action::MoveDown])  vel->y += gait->move_speed;
            if (in.held[(int)Action::MoveLeft])  vel->x -= gait->move_speed;
            if (in.held[(int)Action::MoveRight]) vel->x += gait->move_speed;

            t->x += vel->x * dt;
            t->y += vel->y * dt;

            float spd = sqrtf(vel->x * vel->x + vel->y * vel->y);
            if (spd > 0.001f)
                t->facing = atan2f(vel->y, vel->x);

            // Speed-adaptive step duration: fast → shorter, slow → longer
            constexpr float SLOW_DUR = 0.45f;
            constexpr float FAST_DUR = 0.22f;
            float t_blend = std::min(spd / gait->move_speed, 1.0f);
            gait->step_duration = SLOW_DUR + (FAST_DUR - SLOW_DUR) * t_blend;
        });

    // -----------------------------------------------------------------------
    // 2. ActorGroundingSystem
    // -----------------------------------------------------------------------
    ecs.system("ActorGroundingSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            float dt = ecs.delta_time();
            ecs.each([&](ActorTag, Transform &t, const ActorConfig &cfg) {
                float target_z = sample_world_height(*map_data, t.x, t.y)
                                 + cfg.leg_len + cfg.shin_len;
                // Distance-adaptive blend: sharper correction when far from terrain
                float dist    = fabsf(target_z - t.z);
                float k       = 8.0f + dist * 4.0f; // steeper when further away
                t.z = t.z + (target_z - t.z) * (1.0f - expf(-k * dt));
            });
        });

    // -----------------------------------------------------------------------
    // 3. GaitSystem
    // -----------------------------------------------------------------------
    ecs.system("GaitSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform &t,
                         const Velocity &vel,
                         ProceduralGait &gait,
                         LegState &legs,
                         const ActorConfig &cfg) {

                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
                gait.phase += speed * dt * (glm::two_pi<float>() / (2.0f * gait.stride_len));

                float fwd_x  =  cosf(t.facing), fwd_y  = sinf(t.facing);
                float vel_dx = speed > 0.001f ? vel.x / speed : fwd_x;
                float vel_dy = speed > 0.001f ? vel.y / speed : fwd_y;

                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                float hip_sign[2] = { -1.0f, 1.0f }; // left, right

                for (int leg = 0; leg < 2; ++leg) {
                    float hip_x = t.x + rght_x * hip_sign[leg] * cfg.hip_width;
                    float hip_y = t.y + rght_y * hip_sign[leg] * cfg.hip_width;

                    float stride_off_x = vel_dx * gait.stride_len * 0.5f;
                    float stride_off_y = vel_dy * gait.stride_len * 0.5f;
                    float pred_x = hip_x + stride_off_x;
                    float pred_y = hip_y + stride_off_y;
                    float pred_z = sample_world_height(*map_data, pred_x, pred_y);

                    if (!legs.stepping[leg]) {
                        // One-foot-planted invariant: only step if other foot is planted
                        bool other_planted = !legs.stepping[1 - leg];
                        if (other_planted) {
                            float dx   = legs.foot[leg].x - pred_x;
                            float dy   = legs.foot[leg].y - pred_y;
                            float dist = sqrtf(dx * dx + dy * dy);
                            if (dist > gait.stride_len * 0.5f) {
                                legs.stepping[leg]  = true;
                                legs.progress[leg]  = 0.0f;
                                legs.prev_foot[leg] = legs.foot[leg];
                                legs.target[leg]    = {pred_x, pred_y, pred_z};
                            }
                        }
                    }

                    if (legs.stepping[leg]) {
                        legs.progress[leg] += dt / gait.step_duration;
                        float progress = std::min(legs.progress[leg], 1.0f);
                        // Cubic smoothstep
                        float ts = progress * progress * (3.0f - 2.0f * progress);

                        legs.foot[leg].x = legs.prev_foot[leg].x
                            + (legs.target[leg].x - legs.prev_foot[leg].x) * ts;
                        legs.foot[leg].y = legs.prev_foot[leg].y
                            + (legs.target[leg].y - legs.prev_foot[leg].y) * ts;
                        legs.foot[leg].z = legs.prev_foot[leg].z
                            + (legs.target[leg].z - legs.prev_foot[leg].z) * ts
                            + sinf(progress * glm::pi<float>()) * gait.step_height;

                        if (legs.progress[leg] >= 1.0f) {
                            legs.stepping[leg] = false;
                            legs.foot[leg]     = legs.target[leg];
                        }
                    }
                }
            });
        });

    // -----------------------------------------------------------------------
    // 4. IKSystem
    // -----------------------------------------------------------------------
    ecs.system("IKSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([&](ActorTag,
                         const Transform &t,
                         const Velocity &vel,
                         const LegState &legs,
                         const ActorConfig &cfg,
                         AnimationState &anim,
                         SkeletonPose &pose) {

                using J = Joint;

                float facing = t.facing;
                float fwd_x  =  cosf(facing), fwd_y  = sinf(facing);
                float rght_x = -sinf(facing), rght_y = cosf(facing);

                float dt    = 1.0f / 60.0f; // use fixed dt for IK spring stability
                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);

                // Root and spine chain
                glm::vec3 root(t.x, t.y, t.z);
                glm::vec3 spine  = root  + glm::vec3(0, 0, cfg.torso_len * 0.4f);
                glm::vec3 chest  = root  + glm::vec3(0, 0, cfg.torso_len);
                glm::vec3 neck   = chest + glm::vec3(0, 0, cfg.neck_len);
                glm::vec3 head   = neck  + glm::vec3(0, 0, cfg.head_radius);

                pose.joints[(int)J::ROOT]  = root;
                pose.joints[(int)J::SPINE] = spine;
                pose.joints[(int)J::CHEST] = chest;
                pose.joints[(int)J::NECK]  = neck;
                pose.joints[(int)J::HEAD]  = head;

                // Hip sockets
                glm::vec3 l_hip(t.x - rght_x * cfg.hip_width, t.y - rght_y * cfg.hip_width, t.z);
                glm::vec3 r_hip(t.x + rght_x * cfg.hip_width, t.y + rght_y * cfg.hip_width, t.z);
                pose.joints[(int)J::L_HIP] = l_hip;
                pose.joints[(int)J::R_HIP] = r_hip;

                // Shoulder sockets
                glm::vec3 l_shoulder(chest.x - rght_x * cfg.shoulder_width,
                                     chest.y - rght_y * cfg.shoulder_width, chest.z);
                glm::vec3 r_shoulder(chest.x + rght_x * cfg.shoulder_width,
                                     chest.y + rght_y * cfg.shoulder_width, chest.z);
                pose.joints[(int)J::L_SHOULDER] = l_shoulder;
                pose.joints[(int)J::R_SHOULDER] = r_shoulder;

                // Pendulum arm swing: advance arm phases
                float phase_rate = glm::two_pi<float>() / (2.0f * 0.60f); // matches stride_len default
                anim.arm_phase[0] += speed * dt * phase_rate;
                anim.arm_phase[1] += speed * dt * phase_rate;

                // Left arm: sin(arm_phase[0]), right arm: sin(arm_phase[1])
                // arm_phase[1] starts at pi, so they swing in antiphase
                float arm_swing_scale = cfg.arm_len * 0.3f;

                // Forearm delayed by SmoothDamp spring (joint delay chain)
                float target_delay_0 = anim.arm_phase[0];
                float target_delay_1 = anim.arm_phase[1];
                anim.arm_delay[0] = smooth_damp(anim.arm_delay[0], target_delay_0,
                                                anim.arm_delay_vel[0], 0.08f, dt);
                anim.arm_delay[1] = smooth_damp(anim.arm_delay[1], target_delay_1,
                                                anim.arm_delay_vel[1], 0.08f, dt);

                // Left arm joints
                float l_swing_fwd = sinf(anim.arm_phase[0]) * arm_swing_scale;
                float l_fore_fwd  = sinf(anim.arm_delay[0]) * arm_swing_scale;
                glm::vec3 l_fwd_vec(fwd_x * l_swing_fwd, fwd_y * l_swing_fwd, 0.0f);
                glm::vec3 l_fore_fwd_vec(fwd_x * l_fore_fwd, fwd_y * l_fore_fwd, 0.0f);

                pose.joints[(int)J::L_ELBOW] = l_shoulder + l_fwd_vec
                                               + glm::vec3(0, 0, -cfg.arm_len * 0.8f);
                pose.joints[(int)J::L_WRIST] = pose.joints[(int)J::L_ELBOW]
                                               + l_fore_fwd_vec
                                               + glm::vec3(0, 0, -cfg.forearm_len * 0.8f);

                // Right arm joints
                float r_swing_fwd = sinf(anim.arm_phase[1]) * arm_swing_scale;
                float r_fore_fwd  = sinf(anim.arm_delay[1]) * arm_swing_scale;
                glm::vec3 r_fwd_vec(fwd_x * r_swing_fwd, fwd_y * r_swing_fwd, 0.0f);
                glm::vec3 r_fore_fwd_vec(fwd_x * r_fore_fwd, fwd_y * r_fore_fwd, 0.0f);

                pose.joints[(int)J::R_ELBOW] = r_shoulder + r_fwd_vec
                                               + glm::vec3(0, 0, -cfg.arm_len * 0.8f);
                pose.joints[(int)J::R_WRIST] = pose.joints[(int)J::R_ELBOW]
                                               + r_fore_fwd_vec
                                               + glm::vec3(0, 0, -cfg.forearm_len * 0.8f);

                // Leg IK — two-bone analytical solver
                auto solve_leg = [&](glm::vec3 H, glm::vec3 foot_target,
                                     float a, float b,
                                     glm::vec3 pole,
                                     glm::vec3 &out_knee, glm::vec3 &out_ankle) {
                    out_ankle = foot_target;

                    glm::vec3 axis = foot_target - H;
                    float D = glm::length(axis);
                    float min_D = fabsf(a - b) + 0.001f;
                    float max_D = a + b - 0.001f;
                    D = std::max(min_D, std::min(D, max_D));

                    if (glm::length(axis) > 1e-5f)
                        axis = glm::normalize(axis) * D;
                    else
                        axis = glm::vec3(0, 0, -D);

                    float cos_alpha = (a * a + D * D - b * b) / (2.0f * a * D);
                    cos_alpha = std::max(-1.0f, std::min(1.0f, cos_alpha));
                    float alpha = acosf(cos_alpha);

                    glm::vec3 axis_n   = glm::normalize(axis);
                    glm::vec3 pole_off = pole - H;
                    glm::vec3 perp     = pole_off - glm::dot(pole_off, axis_n) * axis_n;
                    if (glm::length(perp) > 1e-5f)
                        perp = glm::normalize(perp);
                    else
                        perp = glm::vec3(rght_x, rght_y, 0.0f);

                    glm::vec3 dir_to_knee = axis_n * cosf(alpha) + perp * sinf(alpha);
                    out_knee = H + dir_to_knee * a;
                };

                glm::vec3 l_pole = l_hip + glm::vec3(-rght_x * 0.5f, -rght_y * 0.5f, 0.2f);
                glm::vec3 l_knee, l_ankle;
                solve_leg(l_hip, legs.foot[0], cfg.leg_len, cfg.shin_len, l_pole, l_knee, l_ankle);
                pose.joints[(int)J::L_KNEE]  = l_knee;
                pose.joints[(int)J::L_ANKLE] = l_ankle;

                glm::vec3 r_pole = r_hip + glm::vec3(rght_x * 0.5f, rght_y * 0.5f, 0.2f);
                glm::vec3 r_knee, r_ankle;
                solve_leg(r_hip, legs.foot[1], cfg.leg_len, cfg.shin_len, r_pole, r_knee, r_ankle);
                pose.joints[(int)J::R_KNEE]  = r_knee;
                pose.joints[(int)J::R_ANKLE] = r_ankle;
            });
        });

    // -----------------------------------------------------------------------
    // 5. SkeletonFinaliseSystem
    // -----------------------------------------------------------------------
    ecs.system("SkeletonFinaliseSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([&](ActorTag,
                         const Transform &t,
                         const Velocity &vel,
                         const ActorConfig &cfg,
                         AnimationState &anim,
                         SkeletonPose &pose) {

                using J = Joint;
                float dt    = ecs.delta_time();
                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);

                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                // --- Velocity smoothing (SmoothDamp) ---
                anim.prev_smooth_vel = anim.smooth_vel;
                anim.smooth_vel.x = smooth_damp(anim.smooth_vel.x, vel.x,
                                                anim.vel_vel.x, 0.12f, dt);
                anim.smooth_vel.y = smooth_damp(anim.smooth_vel.y, vel.y,
                                                anim.vel_vel.y, 0.12f, dt);

                float smooth_speed = sqrtf(anim.smooth_vel.x * anim.smooth_vel.x
                                           + anim.smooth_vel.y * anim.smooth_vel.y);

                // --- Acceleration-driven torso lean ---
                // Acceleration = change in smooth velocity / dt
                float accel_x = (dt > 1e-6f) ? (anim.smooth_vel.x - anim.prev_smooth_vel.x) / dt : 0.0f;
                float accel_y = (dt > 1e-6f) ? (anim.smooth_vel.y - anim.prev_smooth_vel.y) / dt : 0.0f;

                // Project acceleration onto facing direction for fwd lean
                float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
                float accel_fwd  = accel_x * fwd_x  + accel_y * fwd_y;
                float accel_side = accel_x * rght_x + accel_y * rght_y;

                float lean_scale = 0.006f;
                anim.lean_x = accel_fwd  * fwd_x  * lean_scale
                            + accel_side * rght_x * lean_scale;
                anim.lean_y = accel_fwd  * fwd_y  * lean_scale
                            + accel_side * rght_y * lean_scale;

                // Successive spine breaking: lean applied with escalating weights
                // SPINE 30%, CHEST 62%, NECK+HEAD 100%
                glm::vec3 lean_vec(anim.lean_x, anim.lean_y, 0.0f);
                pose.joints[(int)J::SPINE] += lean_vec * 0.30f;
                pose.joints[(int)J::CHEST] += lean_vec * 0.62f;
                pose.joints[(int)J::NECK]  += lean_vec;
                pose.joints[(int)J::HEAD]  += lean_vec;

                // --- CoM hip sway (uses smooth_speed) ---
                anim.sway_phase += smooth_speed * dt * 6.0f;
                float sway = sinf(anim.sway_phase) * anim.sway_amt;
                glm::vec3 sway_vec(rght_x * sway, rght_y * sway, 0.0f);
                pose.joints[(int)J::ROOT]  += sway_vec;
                pose.joints[(int)J::SPINE] += sway_vec;
                pose.joints[(int)J::CHEST] += sway_vec;

                // --- Idle micro-motion (only when nearly still) ---
                constexpr float IDLE_THRESHOLD = 0.5f;
                if (speed < IDLE_THRESHOLD) {
                    float idle_blend = 1.0f - (speed / IDLE_THRESHOLD);

                    // Breathing: 0.25 Hz cosine lift on chest/neck/head
                    anim.breath_phase += glm::two_pi<float>() * 0.25f * dt;
                    float breath_lift = cosf(anim.breath_phase) * 0.012f * idle_blend;
                    glm::vec3 breath_vec(0.0f, 0.0f, breath_lift);
                    pose.joints[(int)J::CHEST] += breath_vec;
                    pose.joints[(int)J::NECK]  += breath_vec;
                    pose.joints[(int)J::HEAD]  += breath_vec;

                    // Weight-shift: 0.18 Hz lateral sway on ROOT
                    anim.weight_phase += glm::two_pi<float>() * 0.18f * dt;
                    float weight_sway = sinf(anim.weight_phase) * 0.02f * idle_blend;
                    glm::vec3 weight_vec(rght_x * weight_sway, rght_y * weight_sway, 0.0f);
                    pose.joints[(int)J::ROOT]  += weight_vec;
                    pose.joints[(int)J::SPINE] += weight_vec;
                }
            });
        });

    // -----------------------------------------------------------------------
    // 6. AnimationLogSystem
    // -----------------------------------------------------------------------
    ecs.system("AnimationLogSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, player_entity, &anim_log](flecs::iter &) {
            if (!anim_log.active) return;
            if (!player_entity.is_alive()) return;

            const auto *t    = player_entity.get<Transform>();
            const auto *vel  = player_entity.get<Velocity>();
            const auto *gait = player_entity.get<ProceduralGait>();
            const auto *legs = player_entity.get<LegState>();
            const auto *pose = player_entity.get<SkeletonPose>();
            const auto *cfg  = player_entity.get<ActorConfig>();
            const auto *anim = player_entity.get<AnimationState>();
            if (!t || !vel || !gait || !legs || !pose || !cfg || !anim) return;

            float dt = ecs.delta_time();
            anim_log.begin_frame(dt);
            anim_log.log_transform(*t, *vel);
            anim_log.log_gait(*gait);
            anim_log.log_legs(*legs, *t, *cfg);
            anim_log.log_joints(*pose, *t);
            anim_log.log_finalize(anim->sway_phase, anim->sway_amt,
                                  anim->lean_x, anim->lean_y);
            anim_log.log_dynamics(*anim, dt);
            anim_log.log_arm_swing(*pose, *t, *anim);
            anim_log.log_grounding(*t);
            anim_log.end_frame();
        });
}
