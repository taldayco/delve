#include "render/actor_animation.h"
#include "actor.h"
#include "animation_log.h"
#include "game_state.h"
#include "terrain/map_data.h"
#include "terrain/map_util.h"
#include "input/input.h"

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

// Critically-damped spring smoothing (Game Programming Gems 4).
// Returns the new current value; vel_ref is updated in-place.
static float smooth_damp(float current, float target, float& vel_ref,
                          float smooth_time, float dt) {
    smooth_time = std::max(smooth_time, 0.0001f);
    float omega   = 2.0f / smooth_time;
    float x       = omega * dt;
    float exp_x   = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change  = current - target;
    float temp    = (vel_ref + omega * change) * dt;
    vel_ref       = (vel_ref - omega * temp) * exp_x;
    return target + (change + temp) * exp_x;
}

void register_animation_systems(flecs::world& ecs,
                                flecs::entity& player_entity,
                                InputSystem& input,
                                AnimationLogger& anim_log) {

    // 1. PlayerMovementSystem — reads input, updates velocity + smooth_vel,
    //    computes speed-adaptive step duration.
    ecs.system("PlayerMovementSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, &player_entity, &input](flecs::iter&) {
            auto* phase = ecs.get<GamePhase>();
            if (!phase || phase->current != GamePhase::Playing) return;
            if (!player_entity.is_alive()) return;

            auto* t    = player_entity.get_mut<Transform>();
            auto* vel  = player_entity.get_mut<Velocity>();
            auto* gait = player_entity.get_mut<ProceduralGait>();
            auto* anim = player_entity.get_mut<AnimationState>();
            if (!t || !vel || !gait || !anim) return;

            auto& in = input.state();
            float dt = ecs.delta_time();

            // Raw velocity from input.
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

            // SmoothDamp velocity for animation state.
            const float smooth_t = 0.12f;
            anim->smooth_vel.x = smooth_damp(anim->smooth_vel.x, vel->x,
                                              anim->vel_vel.x, smooth_t, dt);
            anim->smooth_vel.y = smooth_damp(anim->smooth_vel.y, vel->y,
                                              anim->vel_vel.y, smooth_t, dt);

            // Speed-adaptive step duration: lerp 0.45s (slow) → 0.22s (fast).
            const float dur_slow = 0.45f;
            const float dur_fast = 0.22f;
            float t_norm = std::min(spd / gait->move_speed, 1.0f);
            gait->step_duration = dur_slow + t_norm * (dur_fast - dur_slow);
        });

    // 2. ActorGroundingSystem — snaps actor Z to terrain height with
    //    adaptive blend (sharper correction when far from ground).
    ecs.system("ActorGroundingSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter&) {
            const auto* map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            float dt = ecs.delta_time();
            ecs.each([&](ActorTag, Transform& t, const ActorConfig& cfg) {
                float target_z = sample_world_height(*map_data, t.x, t.y)
                                 + cfg.leg_len + cfg.shin_len;
                float dist  = fabsf(target_z - t.z);
                float alpha = dist > 0.3f ? 0.25f : (1.0f - expf(-8.0f * dt));
                t.z = t.z + (target_z - t.z) * alpha;
            });
        });

    // 3. GaitSystem — advances gait phase, triggers foot steps.
    //    One-foot-planted invariant: skips new step if other foot is stepping.
    ecs.system("GaitSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter&) {
            const auto* map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform& t,
                         const Velocity& vel,
                         ProceduralGait& gait,
                         LegState& legs,
                         AnimationState& anim,
                         const ActorConfig& cfg) {

                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
                gait.phase += speed * dt * (glm::two_pi<float>() / (2.0f * gait.stride_len));

                float fwd_x  =  cosf(t.facing), fwd_y  = sinf(t.facing);
                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);
                float vel_dx = speed > 0.001f ? vel.x / speed : fwd_x;
                float vel_dy = speed > 0.001f ? vel.y / speed : fwd_y;

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
                        float dx   = legs.foot[leg].x - pred_x;
                        float dy   = legs.foot[leg].y - pred_y;
                        float dist = sqrtf(dx * dx + dy * dy);
                        // One-foot-planted: only start step if other foot is NOT stepping.
                        if (dist > gait.stride_len * 0.5f && !legs.stepping[1 - leg]) {
                            legs.stepping[leg]  = true;
                            legs.progress[leg]  = 0.0f;
                            legs.prev_foot[leg] = legs.foot[leg];
                            legs.target[leg]    = {pred_x, pred_y, pred_z};
                        }
                    }

                    if (legs.stepping[leg]) {
                        legs.progress[leg] += dt / gait.step_duration;
                        float progress = std::min(legs.progress[leg], 1.0f);
                        float ts = progress * progress * (3.0f - 2.0f * progress);

                        legs.foot[leg].x = legs.prev_foot[leg].x
                                         + (legs.target[leg].x - legs.prev_foot[leg].x) * ts;
                        legs.foot[leg].y = legs.prev_foot[leg].y
                                         + (legs.target[leg].y - legs.prev_foot[leg].y) * ts;
                        legs.foot[leg].z = legs.prev_foot[leg].z
                                         + (legs.target[leg].z - legs.prev_foot[leg].z) * ts
                                         + sinf(progress * glm::pi<float>()) * gait.step_height;

                        if (legs.progress[leg] >= 1.0f) {
                            // Record foot velocity at contact for grounding metrics.
                            glm::vec3 travel = legs.target[leg] - legs.prev_foot[leg];
                            anim.foot_contact_velocity[leg] =
                                glm::length(travel) / std::max(gait.step_duration, 0.001f);
                            legs.stepping[leg] = false;
                            legs.foot[leg]     = legs.target[leg];
                        }
                    }
                }
            });
        });

    // 4. IKSystem — two-bone leg IK, spine chain, pendulum arm swing with
    //    SmoothDamp forearm lag.
    ecs.system("IKSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter&) {
            ecs.each([&](ActorTag,
                         const Transform& t,
                         const LegState& legs,
                         const ActorConfig& cfg,
                         const ProceduralGait& gait,
                         AnimationState& anim,
                         SkeletonPose& pose) {

                float dt = ecs.delta_time();
                using J  = Joint;

                float facing = t.facing;
                float fwd_x  =  cosf(facing), fwd_y  = sinf(facing);
                float rght_x = -sinf(facing), rght_y = cosf(facing);

                // Root and spine chain.
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

                // Hip sockets.
                glm::vec3 l_hip(t.x - rght_x * cfg.hip_width,
                                t.y - rght_y * cfg.hip_width, t.z);
                glm::vec3 r_hip(t.x + rght_x * cfg.hip_width,
                                t.y + rght_y * cfg.hip_width, t.z);
                pose.joints[(int)J::L_HIP] = l_hip;
                pose.joints[(int)J::R_HIP] = r_hip;

                // Shoulder sockets.
                glm::vec3 l_shoulder(chest.x - rght_x * cfg.shoulder_width,
                                     chest.y - rght_y * cfg.shoulder_width, chest.z);
                glm::vec3 r_shoulder(chest.x + rght_x * cfg.shoulder_width,
                                     chest.y + rght_y * cfg.shoulder_width, chest.z);
                pose.joints[(int)J::L_SHOULDER] = l_shoulder;
                pose.joints[(int)J::R_SHOULDER] = r_shoulder;

                // Two-bone analytical IK solver.
                auto solve_leg = [&](glm::vec3 H, glm::vec3 foot_target,
                                     float a, float b,
                                     glm::vec3 pole,
                                     glm::vec3& out_knee, glm::vec3& out_ankle) {
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
                    glm::vec3 axis_n = glm::normalize(axis);
                    glm::vec3 pole_off = pole - H;
                    glm::vec3 perp = pole_off - glm::dot(pole_off, axis_n) * axis_n;
                    if (glm::length(perp) > 1e-5f)
                        perp = glm::normalize(perp);
                    else
                        perp = glm::vec3(rght_x, rght_y, 0.0f);
                    glm::vec3 dir_to_knee = axis_n * cosf(alpha) + perp * sinf(alpha);
                    out_knee = H + dir_to_knee * a;
                };

                // Left leg.
                glm::vec3 l_pole = l_hip + glm::vec3(-rght_x * 0.5f, -rght_y * 0.5f, 0.2f);
                glm::vec3 l_knee, l_ankle;
                solve_leg(l_hip, legs.foot[0], cfg.leg_len, cfg.shin_len, l_pole,
                          l_knee, l_ankle);
                pose.joints[(int)J::L_KNEE]  = l_knee;
                pose.joints[(int)J::L_ANKLE] = l_ankle;

                // Right leg.
                glm::vec3 r_pole = r_hip + glm::vec3(rght_x * 0.5f, rght_y * 0.5f, 0.2f);
                glm::vec3 r_knee, r_ankle;
                solve_leg(r_hip, legs.foot[1], cfg.leg_len, cfg.shin_len, r_pole,
                          r_knee, r_ankle);
                pose.joints[(int)J::R_KNEE]  = r_knee;
                pose.joints[(int)J::R_ANKLE] = r_ankle;

                // Pendulum arm swing.
                float speed = sqrtf(anim.smooth_vel.x * anim.smooth_vel.x
                                  + anim.smooth_vel.y * anim.smooth_vel.y);
                float phase_rate = speed > 0.001f
                    ? speed * (glm::two_pi<float>() / (2.0f * gait.stride_len))
                    : 0.3f; // gentle idle sway

                float swing_amp = speed > 0.001f
                    ? std::min(speed * 0.08f, 0.18f)
                    : 0.02f;

                // Arm phases advance monotonically in radians.
                // arm_phase[1] is initialized to π so arms stay 180° antiphase.
                // No fmod — keeping phases monotonic avoids SmoothDamp wraparound snap.
                anim.arm_phase[0] += phase_rate * dt;
                anim.arm_phase[1] += phase_rate * dt;

                // SmoothDamp forearm lag (arm_delay_vel is the spring velocity state).
                const float arm_delay_t = 0.15f;
                anim.arm_delay[0] = smooth_damp(anim.arm_delay[0], anim.arm_phase[0],
                                                 anim.arm_delay_vel[0], arm_delay_t, dt);
                anim.arm_delay[1] = smooth_damp(anim.arm_delay[1], anim.arm_phase[1],
                                                 anim.arm_delay_vel[1], arm_delay_t, dt);

                for (int arm = 0; arm < 2; ++arm) {
                    float side  = (arm == 0) ? -1.0f : 1.0f;
                    glm::vec3& shoulder = (arm == 0) ? l_shoulder : r_shoulder;

                    // Upper arm swings forward/back.
                    float upper_swing = sinf(anim.arm_phase[arm]) * swing_amp;
                    // Forearm lags behind — uses delayed phase.
                    float lower_swing = sinf(anim.arm_delay[arm]) * swing_amp * 0.7f;
                    float elbow_bend  = std::max(0.0f,
                                                 sinf(anim.arm_phase[arm]) * 0.06f);

                    glm::vec3 elbow = shoulder
                        + glm::vec3(fwd_x * upper_swing, fwd_y * upper_swing,
                                    -cfg.arm_len * 0.8f + elbow_bend);
                    glm::vec3 wrist = elbow
                        + glm::vec3(fwd_x * lower_swing, fwd_y * lower_swing,
                                    -cfg.forearm_len * 0.8f);

                    if (arm == 0) {
                        pose.joints[(int)J::L_ELBOW] = elbow;
                        pose.joints[(int)J::L_WRIST] = wrist;
                    } else {
                        pose.joints[(int)J::R_ELBOW] = elbow;
                        pose.joints[(int)J::R_WRIST] = wrist;
                    }
                }
            });
        });

    // 5. SkeletonFinaliseSystem — applies hip sway, acceleration-driven torso
    //    lean with successive spine weights, idle breathing, weight-shift.
    ecs.system("SkeletonFinaliseSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter&) {
            ecs.each([&](ActorTag,
                         const Transform& t,
                         const Velocity& vel,
                         const ActorConfig& cfg,
                         const ProceduralGait& gait,
                         AnimationState& anim,
                         SkeletonPose& pose) {

                using J = Joint;
                float dt = ecs.delta_time();

                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                // Hip sway — lateral CoM shift during walk.
                anim.sway_phase += speed * dt * 6.0f;
                float sway_target = speed > 0.001f ? 0.04f : 0.0f;
                anim.sway_amt = anim.sway_amt + (sway_target - anim.sway_amt) * 0.08f;
                float sway = sinf(anim.sway_phase) * anim.sway_amt;
                glm::vec3 sway_vec(rght_x * sway, rght_y * sway, 0.0f);
                pose.joints[(int)J::ROOT]  += sway_vec;
                pose.joints[(int)J::SPINE] += sway_vec;
                pose.joints[(int)J::CHEST] += sway_vec;

                // Acceleration-driven torso lean via successive spine weights.
                glm::vec2 accel = (anim.smooth_vel - anim.prev_smooth_vel) / std::max(dt, 0.001f);
                anim.prev_smooth_vel = anim.smooth_vel;

                float target_lean_x = -accel.x * 0.01f - anim.smooth_vel.x * 0.015f;
                float target_lean_y = -accel.y * 0.01f - anim.smooth_vel.y * 0.015f;
                anim.lean_x = anim.lean_x + (target_lean_x - anim.lean_x) * 0.12f;
                anim.lean_y = anim.lean_y + (target_lean_y - anim.lean_y) * 0.12f;

                // Successive breaking: spine gets partial lean, chest full lean.
                const float spine_weights[3] = {0.30f, 0.62f, 1.00f};
                const Joint spine_joints[3]  = {J::SPINE, J::CHEST, J::NECK};
                for (int s = 0; s < 3; ++s) {
                    pose.joints[(int)spine_joints[s]].x += anim.lean_x * spine_weights[s];
                    pose.joints[(int)spine_joints[s]].y += anim.lean_y * spine_weights[s];
                }
                pose.joints[(int)J::HEAD].x += anim.lean_x;
                pose.joints[(int)J::HEAD].y += anim.lean_y;

                // Idle breathing micro-motion — cosine chest lift.
                float idle_factor = std::max(0.0f, 1.0f - speed * 0.8f);
                anim.breath_phase = fmodf(anim.breath_phase + dt * 0.25f, 1.0f);
                const float breath_amp = 0.012f;
                float breath = cosf(anim.breath_phase * glm::two_pi<float>())
                             * breath_amp * idle_factor;
                for (int s = 0; s < 3; ++s)
                    pose.joints[(int)spine_joints[s]].z += breath * spine_weights[s];
                pose.joints[(int)J::HEAD].z += breath;

                // Idle weight-shift — slow lateral sway at rest.
                anim.weight_phase = fmodf(anim.weight_phase + dt * 0.18f, 1.0f);
                const float weight_amp = 0.008f;
                float weight_shift = sinf(anim.weight_phase * glm::two_pi<float>())
                                   * weight_amp * idle_factor;
                pose.joints[(int)J::ROOT].x  += rght_x * weight_shift;
                pose.joints[(int)J::ROOT].y  += rght_y * weight_shift;
                pose.joints[(int)J::SPINE].x += rght_x * weight_shift;
                pose.joints[(int)J::SPINE].y += rght_y * weight_shift;
            });
        });

    // 6. AnimationLogSystem — runs last, logs all animation state.
    ecs.system("AnimationLogSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, &player_entity, &anim_log](flecs::iter&) {
            if (!anim_log.active) return;
            if (!player_entity.is_alive()) return;

            const auto* t    = player_entity.get<Transform>();
            const auto* vel  = player_entity.get<Velocity>();
            const auto* gait = player_entity.get<ProceduralGait>();
            const auto* legs = player_entity.get<LegState>();
            const auto* pose = player_entity.get<SkeletonPose>();
            const auto* cfg  = player_entity.get<ActorConfig>();
            const auto* anim = player_entity.get<AnimationState>();
            if (!t || !vel || !gait || !legs || !pose || !cfg || !anim) return;

            float dt = ecs.delta_time();
            anim_log.begin_frame(dt);
            anim_log.log_transform(*t, *vel);
            anim_log.log_gait(*gait);
            anim_log.log_legs(*legs, *t, *cfg);
            anim_log.log_joints(*pose, *t);
            anim_log.log_finalize(anim->sway_phase, anim->sway_amt,
                                   anim->lean_x, anim->lean_y);
            anim_log.log_dynamics(anim->smooth_vel, anim->lean_x, anim->lean_y);
            anim_log.log_arm_swing(anim->arm_phase[0], anim->arm_phase[1],
                                    anim->arm_delay[0], anim->arm_delay[1]);
            anim_log.log_grounding(*legs, gait->step_duration);
            anim_log.end_frame();
        });
}
