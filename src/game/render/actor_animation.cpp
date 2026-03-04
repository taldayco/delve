#include "actor_animation.h"
#include "terrain/map_util.h"
#include "game_state.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

// Critically damped spring SmoothDamp (GPG4 approximation).
static float smooth_damp(float current, float target, float &vel_state,
                          float smooth_time, float dt) {
    float omega = 2.0f / smooth_time;
    float x = omega * dt;
    float exp_factor = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float temp = (vel_state + omega * change) * dt;
    vel_state = (vel_state - omega * temp) * exp_factor;
    return target + (change + temp) * exp_factor;
}

void register_animation_systems(
    flecs::world &ecs,
    flecs::entity player_entity,
    InputSystem &input,
    AnimationLogger &anim_log)
{
    // -------------------------------------------------------------------------
    // PlayerMovementSystem
    // -------------------------------------------------------------------------
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

            // Speed-adaptive step duration: fast movement = shorter steps.
            float speed_t = std::min(spd / gait->move_speed, 1.0f);
            gait->step_duration = 0.45f - speed_t * (0.45f - 0.22f);
        });

    // -------------------------------------------------------------------------
    // ActorGroundingSystem
    // -------------------------------------------------------------------------
    ecs.system("ActorGroundingSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            float dt = ecs.delta_time();
            ecs.each([&](ActorTag, Transform &t, const ActorConfig &cfg) {
                float target_z = sample_world_height(*map_data, t.x, t.y)
                                 + cfg.leg_len + cfg.shin_len;
                float dist = fabsf(target_z - t.z);
                float k = 8.0f + dist * 4.0f;
                t.z = t.z + (target_z - t.z) * (1.0f - expf(-k * dt));
            });
        });

    // -------------------------------------------------------------------------
    // GaitSystem
    // -------------------------------------------------------------------------
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

                float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
                float vel_dx = speed > 0.001f ? vel.x / speed : fwd_x;
                float vel_dy = speed > 0.001f ? vel.y / speed : fwd_y;

                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                float hip_sign[2] = { -1.0f, 1.0f };

                for (int leg = 0; leg < 2; ++leg) {
                    float hip_x = t.x + rght_x * hip_sign[leg] * cfg.hip_width;
                    float hip_y = t.y + rght_y * hip_sign[leg] * cfg.hip_width;

                    float stride_off_x = vel_dx * gait.stride_len * 0.5f;
                    float stride_off_y = vel_dy * gait.stride_len * 0.5f;
                    float pred_x = hip_x + stride_off_x;
                    float pred_y = hip_y + stride_off_y;
                    float pred_z = sample_world_height(*map_data, pred_x, pred_y);

                    if (!legs.stepping[leg]) {
                        // One-foot-planted invariant: never step both feet at once.
                        int other = 1 - leg;
                        bool other_planted = !legs.stepping[other];

                        float dx = legs.foot[leg].x - pred_x;
                        float dy = legs.foot[leg].y - pred_y;
                        float dist = sqrtf(dx * dx + dy * dy);

                        if (dist > gait.stride_len * 0.5f && other_planted) {
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

                        legs.foot[leg].x = legs.prev_foot[leg].x + (legs.target[leg].x - legs.prev_foot[leg].x) * ts;
                        legs.foot[leg].y = legs.prev_foot[leg].y + (legs.target[leg].y - legs.prev_foot[leg].y) * ts;
                        legs.foot[leg].z = legs.prev_foot[leg].z + (legs.target[leg].z - legs.prev_foot[leg].z) * ts
                                           + sinf(progress * glm::pi<float>()) * gait.step_height;

                        if (legs.progress[leg] >= 1.0f) {
                            legs.stepping[leg] = false;
                            legs.foot[leg]     = legs.target[leg];
                        }
                    }
                }
            });
        });

    // -------------------------------------------------------------------------
    // IKSystem
    // -------------------------------------------------------------------------
    ecs.system("IKSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform &t,
                         const LegState &legs,
                         const ActorConfig &cfg,
                         AnimationState &anim,
                         SkeletonPose &pose) {

                using J = Joint;

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
                glm::vec3 l_hip(t.x - rght_x * cfg.hip_width, t.y - rght_y * cfg.hip_width, t.z);
                glm::vec3 r_hip(t.x + rght_x * cfg.hip_width, t.y + rght_y * cfg.hip_width, t.z);
                pose.joints[(int)J::L_HIP] = l_hip;
                pose.joints[(int)J::R_HIP] = r_hip;

                // Shoulder sockets.
                glm::vec3 l_shoulder(chest.x - rght_x * cfg.shoulder_width,
                                     chest.y - rght_y * cfg.shoulder_width, chest.z);
                glm::vec3 r_shoulder(chest.x + rght_x * cfg.shoulder_width,
                                     chest.y + rght_y * cfg.shoulder_width, chest.z);
                pose.joints[(int)J::L_SHOULDER] = l_shoulder;
                pose.joints[(int)J::R_SHOULDER] = r_shoulder;

                // Pendulum arm swing: phases advance at fixed angular rate.
                float arm_speed = 3.0f;
                anim.arm_phase[0] += arm_speed * dt;
                anim.arm_phase[1] += arm_speed * dt;

                float swing_amp = 0.12f;
                float l_swing = sinf(anim.arm_phase[0]) * swing_amp;
                float r_swing = sinf(anim.arm_phase[1]) * swing_amp;

                // Forearm lag via SmoothDamp.
                anim.arm_delay[0] = smooth_damp(anim.arm_delay[0], l_swing, anim.arm_delay_vel[0], 0.08f, dt);
                anim.arm_delay[1] = smooth_damp(anim.arm_delay[1], r_swing, anim.arm_delay_vel[1], 0.08f, dt);

                // Elbow = shoulder + swing along facing direction.
                glm::vec3 l_elbow = l_shoulder
                    + glm::vec3(fwd_x * l_swing, fwd_y * l_swing, -cfg.arm_len * 0.8f);
                glm::vec3 r_elbow = r_shoulder
                    + glm::vec3(fwd_x * r_swing, fwd_y * r_swing, -cfg.arm_len * 0.8f);

                // Wrist: forearm lag applied to delayed swing.
                glm::vec3 l_wrist = l_elbow
                    + glm::vec3(fwd_x * anim.arm_delay[0] * 0.5f,
                                fwd_y * anim.arm_delay[0] * 0.5f,
                                -cfg.forearm_len * 0.8f);
                glm::vec3 r_wrist = r_elbow
                    + glm::vec3(fwd_x * anim.arm_delay[1] * 0.5f,
                                fwd_y * anim.arm_delay[1] * 0.5f,
                                -cfg.forearm_len * 0.8f);

                pose.joints[(int)J::L_ELBOW] = l_elbow;
                pose.joints[(int)J::L_WRIST] = l_wrist;
                pose.joints[(int)J::R_ELBOW] = r_elbow;
                pose.joints[(int)J::R_WRIST] = r_wrist;

                // Leg IK — two-bone analytical solver.
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

    // -------------------------------------------------------------------------
    // SkeletonFinaliseSystem
    // -------------------------------------------------------------------------
    ecs.system("SkeletonFinaliseSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform &t,
                         const Velocity &vel,
                         const ActorConfig &cfg,
                         AnimationState &anim,
                         SkeletonPose &pose) {

                using J = Joint;
                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);

                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                // --- Velocity SmoothDamp ---
                anim.smooth_vel_x = smooth_damp(anim.smooth_vel_x, vel.x,
                                                anim.vel_spring_x, 0.12f, dt);
                anim.smooth_vel_y = smooth_damp(anim.smooth_vel_y, vel.y,
                                                anim.vel_spring_y, 0.12f, dt);

                // --- Torso lean ---
                float lean_strength = 0.03f;
                float lean_x = 0.0f, lean_y = 0.0f;
                if (speed > 0.001f) {
                    lean_x = vel.x / speed * lean_strength * speed;
                    lean_y = vel.y / speed * lean_strength * speed;
                }
                anim.lean_x = lean_x;
                anim.lean_y = lean_y;

                // Successive spine breaking: SPINE 30%, CHEST 62%, NECK+HEAD 100%.
                glm::vec3 lean_vec(lean_x, lean_y, 0.0f);
                pose.joints[(int)J::SPINE] += lean_vec * 0.30f;
                pose.joints[(int)J::CHEST] += lean_vec * 0.62f;
                pose.joints[(int)J::NECK]  += lean_vec * 1.00f;
                pose.joints[(int)J::HEAD]  += lean_vec * 1.00f;

                // --- CoM hip sway ---
                anim.sway_phase += speed * dt * 6.0f;
                float sway = sinf(anim.sway_phase) * anim.sway_amt;
                glm::vec3 sway_vec(rght_x * sway, rght_y * sway, 0.0f);
                pose.joints[(int)J::ROOT]  += sway_vec;
                pose.joints[(int)J::SPINE] += sway_vec;
                pose.joints[(int)J::CHEST] += sway_vec;

                // --- Idle breathing (fades at speed) ---
                float idle_blend = std::max(0.0f, 1.0f - speed / 2.0f);
                anim.breath_phase += 0.25f * glm::two_pi<float>() * dt;
                float breath = cosf(anim.breath_phase) * 0.012f * idle_blend;
                pose.joints[(int)J::CHEST].z += breath;
                pose.joints[(int)J::NECK].z  += breath;
                pose.joints[(int)J::HEAD].z  += breath;

                // --- Idle weight-shift (fades at speed) ---
                anim.weight_phase += 0.18f * glm::two_pi<float>() * dt;
                float weight_sway = sinf(anim.weight_phase) * 0.015f * idle_blend;
                glm::vec3 weight_vec(rght_x * weight_sway, rght_y * weight_sway, 0.0f);
                pose.joints[(int)J::ROOT]  += weight_vec;
                pose.joints[(int)J::SPINE] += weight_vec;
            });
        });

    // -------------------------------------------------------------------------
    // AnimationLogSystem
    // -------------------------------------------------------------------------
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
            anim_log.log_dynamics(*anim);
            anim_log.log_arm_swing(*pose, *t, *anim);
            anim_log.log_grounding(*legs, *t);
            anim_log.log_finalize(anim->sway_phase, anim->sway_amt, anim->lean_x, anim->lean_y);
            anim_log.end_frame();
        });
}
