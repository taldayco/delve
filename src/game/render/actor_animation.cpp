#include "actor_animation.h"
#include "../animation_log.h"
#include "../game_state.h"
#include "../../engine/input/input.h"
#include "../terrain/map_data.h"
#include "../terrain/map_util.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Critically-damped spring (Game Programming Gems 4 approximation).
// Smoothly moves current toward target; vel is the spring velocity (stateful).
// ---------------------------------------------------------------------------
static float smooth_damp(float current, float target, float *vel,
                          float smooth_time, float dt) {
    float omega = 2.0f / smooth_time;
    float x     = omega * dt;
    float ef    = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float temp   = (*vel + omega * change) * dt;
    *vel         = (*vel - omega * temp) * ef;
    return target + (change + temp) * ef;
}

// ---------------------------------------------------------------------------

void register_animation_systems(flecs::world   &ecs,
                                 flecs::entity   player_entity,
                                 InputSystem    &input,
                                 AnimationLogger &anim_log) {

    // -----------------------------------------------------------------------
    // PlayerMovementSystem
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
            if (!t || !vel || !gait) return;

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
        });

    // -----------------------------------------------------------------------
    // ActorGroundingSystem
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
                float dist = std::abs(target_z - t.z);
                // Stiffer spring when actor is far from terrain (e.g. falling)
                float k = 8.0f + dist * 4.0f;
                float blend = 1.0f - expf(-k * dt);
                t.z = t.z + (target_z - t.z) * blend;
            });
        });

    // -----------------------------------------------------------------------
    // GaitSystem
    // -----------------------------------------------------------------------
    ecs.system("GaitSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform &t,
                         const Velocity  &vel,
                         ProceduralGait  &gait,
                         LegState        &legs,
                         const ActorConfig &cfg) {

                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
                gait.phase += speed * dt
                              * (glm::two_pi<float>() / (2.0f * gait.stride_len));

                // Speed-adaptive step duration: slower at rest, faster at full sprint
                float speed_t = std::min(speed / gait.move_speed, 1.0f);
                gait.step_duration = 0.45f - speed_t * (0.45f - 0.22f);

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
                        float dx = legs.foot[leg].x - pred_x;
                        float dy = legs.foot[leg].y - pred_y;
                        float dist = sqrtf(dx * dx + dy * dy);
                        if (dist > gait.stride_len * 0.5f) {
                            // One-foot-planted invariant: don't step if other foot is airborne
                            int other = 1 - leg;
                            if (!legs.stepping[other]) {
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
    // IKSystem
    // -----------------------------------------------------------------------
    ecs.system("IKSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([&](ActorTag,
                         const Transform   &t,
                         const LegState    &legs,
                         const ActorConfig &cfg,
                         SkeletonPose      &pose) {

                using J = Joint;

                float facing = t.facing;
                float fwd_x  =  cosf(facing), fwd_y  =  sinf(facing);
                float rght_x = -sinf(facing), rght_y =  cosf(facing);

                // Root and spine chain (vertical)
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
                glm::vec3 l_hip(t.x - rght_x * cfg.hip_width,
                                t.y - rght_y * cfg.hip_width,
                                t.z);
                glm::vec3 r_hip(t.x + rght_x * cfg.hip_width,
                                t.y + rght_y * cfg.hip_width,
                                t.z);
                pose.joints[(int)J::L_HIP] = l_hip;
                pose.joints[(int)J::R_HIP] = r_hip;

                // Shoulder sockets
                glm::vec3 l_shoulder(chest.x - rght_x * cfg.shoulder_width,
                                     chest.y - rght_y * cfg.shoulder_width,
                                     chest.z);
                glm::vec3 r_shoulder(chest.x + rght_x * cfg.shoulder_width,
                                     chest.y + rght_y * cfg.shoulder_width,
                                     chest.z);
                pose.joints[(int)J::L_SHOULDER] = l_shoulder;
                pose.joints[(int)J::R_SHOULDER] = r_shoulder;

                // Arm joints (hang down)
                pose.joints[(int)J::L_ELBOW] = l_shoulder + glm::vec3(0, 0, -cfg.arm_len     * 0.8f);
                pose.joints[(int)J::L_WRIST] = pose.joints[(int)J::L_ELBOW]
                                             + glm::vec3(0, 0, -cfg.forearm_len * 0.8f);
                pose.joints[(int)J::R_ELBOW] = r_shoulder + glm::vec3(0, 0, -cfg.arm_len     * 0.8f);
                pose.joints[(int)J::R_WRIST] = pose.joints[(int)J::R_ELBOW]
                                             + glm::vec3(0, 0, -cfg.forearm_len * 0.8f);

                // Two-bone analytical IK solver
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
                solve_leg(l_hip, legs.foot[0], cfg.leg_len, cfg.shin_len,
                          l_pole, l_knee, l_ankle);
                pose.joints[(int)J::L_KNEE]  = l_knee;
                pose.joints[(int)J::L_ANKLE] = l_ankle;

                glm::vec3 r_pole = r_hip + glm::vec3(rght_x * 0.5f, rght_y * 0.5f, 0.2f);
                glm::vec3 r_knee, r_ankle;
                solve_leg(r_hip, legs.foot[1], cfg.leg_len, cfg.shin_len,
                          r_pole, r_knee, r_ankle);
                pose.joints[(int)J::R_KNEE]  = r_knee;
                pose.joints[(int)J::R_ANKLE] = r_ankle;
            });
        });

    // -----------------------------------------------------------------------
    // SkeletonFinaliseSystem
    // -----------------------------------------------------------------------
    ecs.system("SkeletonFinaliseSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([&](ActorTag,
                         const Transform   &t,
                         const Velocity    &vel,
                         const ActorConfig &cfg,
                         const ProceduralGait &gait,
                         AnimationState    &anim,
                         SkeletonPose      &pose) {

                using J = Joint;
                float dt    = ecs.delta_time();
                float raw_speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);
                float fwd_x  =  cosf(t.facing), fwd_y  = sinf(t.facing);

                // --- 1. Smooth raw velocity via critically-damped spring ---
                const float VEL_SMOOTH = 0.12f;
                anim.smoothed_vx = smooth_damp(anim.smoothed_vx, vel.x,
                                               &anim.smooth_vel_x, VEL_SMOOTH, dt);
                anim.smoothed_vy = smooth_damp(anim.smoothed_vy, vel.y,
                                               &anim.smooth_vel_y, VEL_SMOOTH, dt);
                float smooth_speed = sqrtf(anim.smoothed_vx * anim.smoothed_vx
                                         + anim.smoothed_vy * anim.smoothed_vy);

                // --- 2. CoM hip sway (existing behaviour, but use smooth_speed) ---
                anim.sway_phase += smooth_speed * dt * 6.0f;
                float sway = sinf(anim.sway_phase) * anim.sway_amt;
                glm::vec3 sway_vec(rght_x * sway, rght_y * sway, 0.0f);
                pose.joints[(int)J::ROOT]  += sway_vec;
                pose.joints[(int)J::SPINE] += sway_vec;
                pose.joints[(int)J::CHEST] += sway_vec;

                // --- 3. Acceleration-driven torso lean (successive spine breaking) ---
                const float LEAN_SCALE = 0.03f;
                anim.lean_x = 0.0f;
                anim.lean_y = 0.0f;
                if (smooth_speed > 0.001f) {
                    float lean_mag = LEAN_SCALE * smooth_speed;
                    anim.lean_x = (anim.smoothed_vx / smooth_speed) * lean_mag;
                    anim.lean_y = (anim.smoothed_vy / smooth_speed) * lean_mag;
                }
                glm::vec3 lean_full(anim.lean_x, anim.lean_y, 0.0f);
                // Successive breaking: SPINE 30%, CHEST 62%, NECK/HEAD 100%
                pose.joints[(int)J::SPINE] += lean_full * 0.30f;
                pose.joints[(int)J::CHEST] += lean_full * 0.62f;
                pose.joints[(int)J::NECK]  += lean_full * 1.00f;
                pose.joints[(int)J::HEAD]  += lean_full * 1.00f;

                // --- 4. Pendulum arm swing (contralateral antiphase) ---
                // arm_phase[0]=L, arm_phase[1]=R, initialized at {0, π}
                const float ARM_SWING_AMP = 0.10f * cfg.arm_len;
                // Advance arm phases with gait phase (arm swings opposite to legs)
                float phase_delta = smooth_speed * dt
                                    * (glm::two_pi<float>() / (2.0f * gait.stride_len));
                anim.arm_phase[0] += phase_delta;
                anim.arm_phase[1] += phase_delta;

                for (int arm = 0; arm < 2; ++arm) {
                    Joint shoulder_j = (arm == 0) ? J::L_SHOULDER : J::R_SHOULDER;
                    Joint elbow_j    = (arm == 0) ? J::L_ELBOW    : J::R_ELBOW;
                    Joint wrist_j    = (arm == 0) ? J::L_WRIST    : J::R_WRIST;

                    float swing = sinf(anim.arm_phase[arm]) * ARM_SWING_AMP;
                    glm::vec3 shoulder = pose.joints[(int)shoulder_j];

                    // Elbow swings forward/back along facing direction
                    glm::vec3 elbow_base = shoulder + glm::vec3(0, 0, -cfg.arm_len * 0.8f);
                    glm::vec3 swing_offset(fwd_x * swing, fwd_y * swing, 0.0f);
                    pose.joints[(int)elbow_j] = elbow_base + swing_offset;

                    // Forearm lags behind elbow via SmoothDamp (forearm delay chain)
                    // We store the "lag" as a smoothed version of the swing value
                    float lagged_swing = smooth_damp(
                        sinf(anim.arm_phase[arm] - 0.3f) * ARM_SWING_AMP,
                        swing,
                        &anim.arm_delay_vel[arm],
                        0.08f, dt);
                    glm::vec3 lag_offset(fwd_x * lagged_swing, fwd_y * lagged_swing, 0.0f);
                    pose.joints[(int)wrist_j] =
                        pose.joints[(int)elbow_j]
                        + glm::vec3(0, 0, -cfg.forearm_len * 0.8f)
                        + lag_offset;
                }

                // --- 5. Idle breathing (0.25 Hz cosine chest lift) ---
                const float BREATH_HZ  = 0.25f;
                const float BREATH_AMP = 0.012f;
                anim.breath_phase += dt * BREATH_HZ;
                float breath_blend = std::max(0.0f, 1.0f - smooth_speed / 1.0f);
                float chest_lift   = cosf(anim.breath_phase * glm::two_pi<float>())
                                   * BREATH_AMP * breath_blend;
                pose.joints[(int)J::CHEST].z += chest_lift;
                pose.joints[(int)J::NECK].z  += chest_lift;
                pose.joints[(int)J::HEAD].z  += chest_lift;

                // --- 6. Idle weight-shift micro-motion (0.18 Hz lateral sway on ROOT) ---
                const float WEIGHT_HZ  = 0.18f;
                const float WEIGHT_AMP = 0.015f;
                anim.weight_phase += dt * WEIGHT_HZ;
                float weight_blend = std::max(0.0f, 1.0f - smooth_speed / 0.5f);
                float weight_sway  = sinf(anim.weight_phase * glm::two_pi<float>())
                                   * WEIGHT_AMP * weight_blend;
                pose.joints[(int)J::ROOT].x += rght_x * weight_sway;
                pose.joints[(int)J::ROOT].y += rght_y * weight_sway;
            });
        });

    // -----------------------------------------------------------------------
    // AnimationLogSystem
    // -----------------------------------------------------------------------
    ecs.system("AnimationLogSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, player_entity, &anim_log](flecs::iter &) {
            if (!anim_log.active) return;
            if (!player_entity.is_alive()) return;

            const auto *t     = player_entity.get<Transform>();
            const auto *vel   = player_entity.get<Velocity>();
            const auto *gait  = player_entity.get<ProceduralGait>();
            const auto *legs  = player_entity.get<LegState>();
            const auto *pose  = player_entity.get<SkeletonPose>();
            const auto *cfg   = player_entity.get<ActorConfig>();
            const auto *anim  = player_entity.get<AnimationState>();
            if (!t || !vel || !gait || !legs || !pose || !cfg || !anim) return;

            float dt = ecs.delta_time();
            anim_log.begin_frame(dt);
            anim_log.log_transform(*t, *vel);
            anim_log.log_gait(*gait);
            anim_log.log_legs(*legs, *t, *cfg);
            anim_log.log_joints(*pose, *t);
            anim_log.log_finalize(anim->sway_phase, anim->sway_amt,
                                  anim->lean_x, anim->lean_y);
            anim_log.log_dynamics(anim->smoothed_vx, anim->smoothed_vy,
                                  vel->x - anim->smoothed_vx,
                                  vel->y - anim->smoothed_vy);
            anim_log.log_arm_swing(anim->arm_phase[0], anim->arm_phase[1],
                                   sinf(anim->arm_phase[0]),
                                   sinf(anim->arm_phase[1]));
            float target_z = 0.0f; // grounding target computed per-frame in ActorGroundingSystem
            anim_log.log_grounding(target_z, t->z, 0.0f);
            anim_log.end_frame();
        });
}
