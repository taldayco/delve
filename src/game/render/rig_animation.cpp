#include "rig_animation.h"
#include "rig.h"
#include "animation_log.h"
#include "input/input.h"
#include "camera/camera.h"
#include "terrain/map_util.h"
#include "game_state.h"
#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

// File-scope animation config — single instance shared by GaitSystem and RigRenderer.
static const AnimationConfig s_anim_cfg{};

// Unity-style critically-damped spring smoother.
// Smoothly moves 'current' toward 'target' over time.
// 'velocity' is internal state that must persist across calls.
// smooth_time: ~time to reach target (seconds)
static float smooth_damp(float current, float target, float *velocity,
                          float smooth_time, float dt) {
    float omega = 2.0f / smooth_time;
    float x     = omega * dt;
    float exp_f = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float delta = current - target;
    float temp  = (*velocity + omega * delta) * dt;
    *velocity   = (*velocity - omega * temp) * exp_f;
    return target + (delta + temp) * exp_f;
}

void register_rig_systems(flecs::world &ecs,
                           InputSystem    &input,
                           CameraState    &camera,
                           AnimationLogger &anim_log,
                           flecs::entity   player_entity) {

    // =========================================================================
    // 1. PlayerMovementSystem
    //    Input → SmoothDamp velocity → position update
    // =========================================================================
    ecs.system("PlayerMovementSystem")
        .kind(flecs::PostUpdate)
        .run([&input, &ecs, player_entity](flecs::iter &) {
            auto *phase = ecs.get<GamePhase>();
            if (!phase || phase->current != GamePhase::Playing) return;
            if (!player_entity.is_alive()) return;

            auto *t    = player_entity.get_mut<Transform>();
            auto *vel  = player_entity.get_mut<Velocity>();
            auto *gait = player_entity.get_mut<ProceduralGait>();
            auto *anim = player_entity.get_mut<RigState>();
            if (!t || !vel || !gait || !anim) return;

            auto &in = input.state();
            float dt = ecs.delta_time();

            // Desired velocity from raw input.
            float raw_x = 0.0f, raw_y = 0.0f;
            if (in.held[(int)Action::MoveUp])    raw_y -= 1.0f;
            if (in.held[(int)Action::MoveDown])  raw_y += 1.0f;
            if (in.held[(int)Action::MoveLeft])  raw_x -= 1.0f;
            if (in.held[(int)Action::MoveRight]) raw_x += 1.0f;

            // Rotate -45 degrees to align WASD with isometric screen axes.
            // Isometric camera looks from NE: world(-1,-1) = screen UP.
            // -45° maps W(screen-up) → world(-x,-y), D(screen-right) → world(+x,-y).
            static constexpr float COS_ISO = 0.70710678118f; // cos(45°)
            static constexpr float SIN_ISO = 0.70710678118f; // sin(45°)
            float desired_x = ( raw_x * COS_ISO + raw_y * SIN_ISO) * gait->move_speed;
            float desired_y = (-raw_x * SIN_ISO + raw_y * COS_ISO) * gait->move_speed;

            // SmoothDamp: critically-damped spring gives feeling of mass.
            // smooth_time = 0.1s → reaches ~90% of target in ~0.2s.
            const float smooth_time = 0.1f;
            anim->smooth_velocity.x = smooth_damp(anim->smooth_velocity.x, desired_x,
                                                    &anim->velocity_rate.x, smooth_time, dt);
            anim->smooth_velocity.y = smooth_damp(anim->smooth_velocity.y, desired_y,
                                                    &anim->velocity_rate.y, smooth_time, dt);

            vel->x = anim->smooth_velocity.x;
            vel->y = anim->smooth_velocity.y;

            t->x += vel->x * dt;
            t->y += vel->y * dt;

            float spd = sqrtf(vel->x * vel->x + vel->y * vel->y);
            if (spd > 0.001f)
                t->facing = atan2f(vel->y, vel->x);
        });

    // =========================================================================
    // 2. ActorGroundingSystem
    //    Snap actor Z to terrain height using adaptive spring.
    // =========================================================================
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
                // Stiffer spring when far from ground, softer when close.
                float k = 8.0f + dist * 4.0f;
                t.z = t.z + (target_z - t.z) * (1.0f - expf(-k * dt));
            });
        });

    // =========================================================================
    // 3. GaitSystem
    //    Procedural foot placement with one-foot-planted invariant.
    //    Speed-adaptive step duration for natural cadence.
    // =========================================================================
    ecs.system("GaitSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform &t,
                         const Velocity   &vel,
                         ProceduralGait   &gait,
                         LegState         &legs,
                         RigState         &anim,
                         const ActorConfig &cfg) {

                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);

                float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
                float vel_dx = speed > 0.001f ? vel.x / speed : fwd_x;
                float vel_dy = speed > 0.001f ? vel.y / speed : fwd_y;

                // Gait phase drives arm swing and hip oscillation only (foot
                // placement is distance-based and unaffected by this phase).
                // Rate chosen so full-speed (4 u/s) gives ~1.27 Hz swing — a
                // natural walking cadence.  Direction-independent: the old
                // iso-vertical multiplier made arms/hips speed up depending on
                // screen direction, which looked unnatural.
                // Casual walk cadence: ~1.7 Hz at full speed (4 u/s).
                // 2.7 rad/s per unit speed → ω = 10.8 rad/s → T ≈ 0.58s.
                constexpr float SWING_RATE = 2.7f; // rad/s per unit speed
                gait.phase += speed * dt * SWING_RATE;

                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                // Hip socket XY offsets (legs offset left/right of facing).
                float hip_sign[2] = { -1.0f, 1.0f }; // left, right

                // Speed-adaptive step duration: faster movement = quicker steps.
                float speed_ratio      = std::max(0.4f, std::min(1.0f, speed / gait.move_speed));
                float adaptive_duration = gait.step_duration / speed_ratio;

                // Blend forward prediction toward zero when idle so feet
                // return to a neutral stance directly under the hips.
                float speed_factor = std::min(1.0f, speed / (gait.move_speed * 0.15f));

                for (int leg = 0; leg < 2; ++leg) {
                    int other_leg = 1 - leg;

                    float hip_x = t.x + rght_x * hip_sign[leg] * cfg.hip_width;
                    float hip_y = t.y + rght_y * hip_sign[leg] * cfg.hip_width;

                    // Trigger check: how far the planted foot is from nominal center.
                    // When idle, center collapses to hip position (no forward offset)
                    // so feet that landed ahead during walking trigger return steps.
                    float half_stride = gait.stride_len * 0.5f;
                    float center_off = half_stride * speed_factor;
                    float center_x = hip_x + vel_dx * center_off;
                    float center_y = hip_y + vel_dy * center_off;

                    if (!legs.stepping[leg]) {
                        float dx   = legs.foot[leg].x - center_x;
                        float dy   = legs.foot[leg].y - center_y;
                        float dist = sqrtf(dx * dx + dy * dy);

                        // One-foot-planted invariant: only step if other foot is planted.
                        // Lower trigger threshold when idle so return steps fire sooner.
                        bool other_planted = !legs.stepping[other_leg];
                        float trigger_dist = half_stride * (0.25f + 0.75f * speed_factor);
                        if (dist > trigger_dist && other_planted) {
                            legs.stepping[leg]  = true;
                            legs.progress[leg]  = 0.0f;
                            legs.prev_foot[leg] = legs.foot[leg];

                            // Velocity-predicted target: foot lands ahead of where
                            // the hip will be when the step completes, implementing
                            // heel-strike placement (inverted pendulum model).
                            // When idle, target collapses to hip position (neutral stance).
                            float step_travel = speed * adaptive_duration;
                            float target_off  = (half_stride + step_travel * 0.75f) * speed_factor;
                            float tgt_x = hip_x + vel_dx * target_off;
                            float tgt_y = hip_y + vel_dy * target_off;
                            float tgt_z = sample_world_height(*map_data, tgt_x, tgt_y);
                            legs.target[leg]    = {tgt_x, tgt_y, tgt_z};
                        }
                    }

                    if (legs.stepping[leg]) {
                        legs.progress[leg] += dt / adaptive_duration;
                        float progress = std::min(legs.progress[leg], 1.0f);

                        // Smootherstep interpolation for XY: steeper S-curve than cosine-ease.
                        float warped_t = progress * progress * progress
                                         * (progress * (progress * 6.0f - 15.0f) + 10.0f);

                        // Z height arc: half-sine lift peaking at mid-stride.
                        float ts_z  = progress * progress * (3.0f - 2.0f * progress);

                        legs.foot[leg].x = legs.prev_foot[leg].x + (legs.target[leg].x - legs.prev_foot[leg].x) * warped_t;
                        legs.foot[leg].y = legs.prev_foot[leg].y + (legs.target[leg].y - legs.prev_foot[leg].y) * warped_t;
                        legs.foot[leg].z = legs.prev_foot[leg].z + (legs.target[leg].z - legs.prev_foot[leg].z) * ts_z
                                           + sinf(progress * glm::pi<float>()) * gait.step_height;

                        if (legs.progress[leg] >= 1.0f) {
                            // Log contact velocity for grounding quality metrics.
                            float step_dist = glm::length(legs.target[leg] - legs.prev_foot[leg]);
                            anim.foot_contact_velocity[leg] = step_dist / adaptive_duration;

                            legs.stepping[leg] = false;
                            legs.foot[leg]     = legs.target[leg];
                        }
                    }
                }

                // ---- Planted-foot reach clamp ----
                float max_horiz = gait.stride_len * 0.9f;
                for (int leg = 0; leg < 2; ++leg) {
                    float hip_x = t.x + rght_x * hip_sign[leg] * cfg.hip_width;
                    float hip_y = t.y + rght_y * hip_sign[leg] * cfg.hip_width;

                    if (!legs.stepping[leg]) {
                        float dx = legs.foot[leg].x - hip_x;
                        float dy = legs.foot[leg].y - hip_y;
                        float hd = sqrtf(dx * dx + dy * dy);
                        if (hd > max_horiz) {
                            float s = max_horiz / hd;
                            legs.foot[leg].x = hip_x + dx * s;
                            legs.foot[leg].y = hip_y + dy * s;
                        }
                    } else if (legs.progress[leg] < 0.4f) {
                        float tdx = legs.target[leg].x - hip_x;
                        float tdy = legs.target[leg].y - hip_y;
                        float th  = sqrtf(tdx * tdx + tdy * tdy);
                        if (th > max_horiz) {
                            float step_travel = speed * adaptive_duration;
                            float target_off  = (gait.stride_len * 0.5f
                                                + step_travel * 0.75f) * speed_factor;
                            legs.target[leg].x = hip_x + vel_dx * target_off;
                            legs.target[leg].y = hip_y + vel_dy * target_off;
                            legs.target[leg].z = sample_world_height(*map_data,
                                                    legs.target[leg].x,
                                                    legs.target[leg].y);
                        }
                    }
                }
            });
        });

    // =========================================================================
    // 4. IKSystem
    //    Two-bone analytical leg IK + pendulum arm swing with joint delay chain.
    // =========================================================================
    ecs.system("IKSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform  &t,
                         const Velocity   &vel,
                         const LegState   &legs,
                         const ActorConfig &cfg,
                         const ProceduralGait &gait,
                         RigState         &anim,
                         RigPose          &pose) {

                using J = Joint;

                float facing = t.facing;
                float fwd_x  =  cosf(facing), fwd_y  = sinf(facing);
                float rght_x = -sinf(facing), rght_y = cosf(facing);

                // HIPS and spine chain.
                glm::vec3 hips(t.x, t.y, t.z);
                glm::vec3 spine01 = hips  + glm::vec3(0, 0, cfg.torso_len * 0.4f);
                glm::vec3 chest   = hips  + glm::vec3(0, 0, cfg.torso_len);
                glm::vec3 neck    = chest + glm::vec3(0, 0, cfg.neck_len);
                glm::vec3 head    = neck  + glm::vec3(0, 0, cfg.head_radius);

                pose.joints[(int)J::HIPS]     = hips;
                pose.joints[(int)J::SPINE_01] = spine01;
                pose.joints[(int)J::CHEST]    = chest;
                pose.joints[(int)J::NECK]     = neck;
                pose.joints[(int)J::HEAD]     = head;

                // Hip sockets (L_UPPER_LEG / R_UPPER_LEG).
                glm::vec3 l_upper_leg(t.x - rght_x * cfg.hip_width, t.y - rght_y * cfg.hip_width, t.z);
                glm::vec3 r_upper_leg(t.x + rght_x * cfg.hip_width, t.y + rght_y * cfg.hip_width, t.z);
                pose.joints[(int)J::L_UPPER_LEG] = l_upper_leg;
                pose.joints[(int)J::R_UPPER_LEG] = r_upper_leg;

                // Shoulder sockets (L_UPPER_ARM / R_UPPER_ARM).
                glm::vec3 l_upper_arm(chest.x - rght_x * cfg.shoulder_width,
                                      chest.y - rght_y * cfg.shoulder_width,
                                      chest.z);
                glm::vec3 r_upper_arm(chest.x + rght_x * cfg.shoulder_width,
                                      chest.y + rght_y * cfg.shoulder_width,
                                      chest.z);
                pose.joints[(int)J::L_UPPER_ARM] = l_upper_arm;
                pose.joints[(int)J::R_UPPER_ARM] = r_upper_arm;

                // ---- Pendulum arm swing ----
                float speed       = sqrtf(vel.x * vel.x + vel.y * vel.y);
                float swing_amp   = std::min(1.0f, speed / gait.move_speed) * glm::radians(30.0f);

                float l_arm_target = sinf(gait.phase + glm::pi<float>()) * swing_amp;
                float r_arm_target = sinf(gait.phase)                      * swing_amp;
                anim.l_arm_target = l_arm_target;
                anim.r_arm_target = r_arm_target;

                // Successive breaking (joint delay chain).
                anim.l_shoulder_smooth = smooth_damp(anim.l_shoulder_smooth, l_arm_target,
                                                      &anim.l_shoulder_rate, 0.02f, dt);
                anim.l_elbow_smooth    = smooth_damp(anim.l_elbow_smooth, anim.l_shoulder_smooth,
                                                      &anim.l_elbow_rate,    0.04f, dt);
                anim.l_wrist_smooth    = smooth_damp(anim.l_wrist_smooth, anim.l_elbow_smooth,
                                                      &anim.l_wrist_rate,    0.06f, dt);

                anim.r_shoulder_smooth = smooth_damp(anim.r_shoulder_smooth, r_arm_target,
                                                      &anim.r_shoulder_rate, 0.02f, dt);
                anim.r_elbow_smooth    = smooth_damp(anim.r_elbow_smooth, anim.r_shoulder_smooth,
                                                      &anim.r_elbow_rate,    0.04f, dt);
                anim.r_wrist_smooth    = smooth_damp(anim.r_wrist_smooth, anim.r_elbow_smooth,
                                                      &anim.r_wrist_rate,    0.06f, dt);

                // Apply arm swing: rotate "hang down" vector by swing angle around forward axis.
                glm::vec3 right_axis(rght_x, rght_y, 0.0f);
                glm::vec3 hang_down(0.0f, 0.0f, -1.0f);

                auto swing_elbow_pos = [&](glm::vec3 shoulder, float shoulder_angle,
                                            float elbow_angle, float arm_len) -> glm::vec3 {
                    float ca = cosf(shoulder_angle);
                    float sa = sinf(shoulder_angle);
                    glm::vec3 k = right_axis;
                    glm::vec3 v = hang_down;
                    glm::vec3 shoulder_dir = v * ca + glm::cross(k, v) * sa + k * glm::dot(k, v) * (1.0f - ca);
                    (void)elbow_angle;
                    return shoulder + shoulder_dir * arm_len;
                };

                auto swing_wrist_pos = [&](glm::vec3 elbow_pos, float shoulder_angle,
                                            float elbow_angle_add, float forearm_len) -> glm::vec3 {
                    float total_angle = shoulder_angle - glm::radians(25.0f) + elbow_angle_add * 0.3f;
                    float ca = cosf(total_angle);
                    float sa = sinf(total_angle);
                    glm::vec3 k = right_axis;
                    glm::vec3 v = hang_down;
                    glm::vec3 dir = v * ca + glm::cross(k, v) * sa + k * glm::dot(k, v) * (1.0f - ca);
                    return elbow_pos + dir * forearm_len;
                };

                // Left arm.
                glm::vec3 l_lower_arm = swing_elbow_pos(l_upper_arm, anim.l_shoulder_smooth,
                                                         anim.l_elbow_smooth, cfg.arm_len);
                glm::vec3 l_hand      = swing_wrist_pos(l_lower_arm, anim.l_shoulder_smooth,
                                                         anim.l_wrist_smooth, cfg.forearm_len);
                pose.joints[(int)J::L_LOWER_ARM] = l_lower_arm;
                pose.joints[(int)J::L_HAND]      = l_hand;

                // Right arm.
                glm::vec3 r_lower_arm = swing_elbow_pos(r_upper_arm, anim.r_shoulder_smooth,
                                                         anim.r_elbow_smooth, cfg.arm_len);
                glm::vec3 r_hand      = swing_wrist_pos(r_lower_arm, anim.r_shoulder_smooth,
                                                         anim.r_wrist_smooth, cfg.forearm_len);
                pose.joints[(int)J::R_LOWER_ARM] = r_lower_arm;
                pose.joints[(int)J::R_HAND]      = r_hand;

                // ---- Leg IK — two-bone analytical solver ----
                auto solve_leg = [&](glm::vec3 H, glm::vec3 foot_target,
                                     float a, float b,
                                     glm::vec3 pole,
                                     glm::vec3 &out_knee, glm::vec3 &out_ankle) {
                    out_ankle = foot_target;

                    glm::vec3 axis = foot_target - H;
                    float D = glm::length(axis);
                    float min_D = fabsf(a - b) + 0.001f;
                    float max_D = a + b - 0.005f;

                    float stretch_limit = max_D * 1.15f;
                    if (D > stretch_limit && D > 1e-5f)
                        out_ankle = H + (axis / D) * stretch_limit;

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
                glm::vec3 l_pole = l_upper_leg + glm::vec3(fwd_x * 0.5f - rght_x * 0.08f,
                                                             fwd_y * 0.5f - rght_y * 0.08f, 0.2f);
                glm::vec3 l_lower_leg, l_foot;
                solve_leg(l_upper_leg, legs.foot[0], cfg.leg_len, cfg.shin_len, l_pole, l_lower_leg, l_foot);
                pose.joints[(int)J::L_LOWER_LEG] = l_lower_leg;
                pose.joints[(int)J::L_FOOT]      = l_foot;

                // Right leg.
                glm::vec3 r_pole = r_upper_leg + glm::vec3(fwd_x * 0.5f + rght_x * 0.08f,
                                                             fwd_y * 0.5f + rght_y * 0.08f, 0.2f);
                glm::vec3 r_lower_leg, r_foot;
                solve_leg(r_upper_leg, legs.foot[1], cfg.leg_len, cfg.shin_len, r_pole, r_lower_leg, r_foot);
                pose.joints[(int)J::R_LOWER_LEG] = r_lower_leg;
                pose.joints[(int)J::R_FOOT]      = r_foot;
            });
        });

    // =========================================================================
    // 5. SkeletonFinaliseSystem
    //    Hip sway, acceleration-driven torso lean, idle micro-motion.
    //    Also computes derived joints: ROOT, SPINE_02, HEAD_END, clavicles,
    //    toes, pole targets, and IK goals.
    // =========================================================================
    ecs.system("SkeletonFinaliseSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform      &t,
                         const Velocity       &vel,
                         const ActorConfig    &cfg,
                         const ProceduralGait &gait,
                         RigState             &anim,
                         RigPose              &pose) {

                using J = Joint;

                float speed   = sqrtf(vel.x * vel.x + vel.y * vel.y);
                float rght_x  = -sinf(t.facing), rght_y = cosf(t.facing);

                // ---- CoM hip sway ----
                anim.sway_phase += speed * dt * 6.0f;
                float sway = sinf(anim.sway_phase) * anim.sway_amount;
                glm::vec3 sway_vec(rght_x * sway, rght_y * sway, 0.0f);
                pose.joints[(int)J::HIPS]     += sway_vec;
                pose.joints[(int)J::SPINE_01] += sway_vec;
                pose.joints[(int)J::CHEST]    += sway_vec;

                // Fix 3: Hip counter-animation (CoM shift + double-bounce).
                float walk_blend = std::min(1.0f, speed / (gait.move_speed * 0.3f));
                float target_hip_roll = sinf(gait.phase) * 0.06f * walk_blend;
                anim.hip_roll = smooth_damp(anim.hip_roll, target_hip_roll,
                                            &anim.hip_roll_rate, 0.04f, dt);

                float target_hip_bob = fabsf(sinf(gait.phase)) * 0.018f * walk_blend;
                anim.hip_bob = smooth_damp(anim.hip_bob, target_hip_bob,
                                           &anim.hip_bob_rate, 0.03f, dt);

                // Apply hip roll as a lateral CoM shift.
                float roll_shift = sinf(anim.hip_roll) * cfg.hip_width * 0.4f;
                glm::vec3 roll_vec(rght_x * roll_shift, rght_y * roll_shift, 0.0f);
                pose.joints[(int)J::HIPS]        += roll_vec;
                pose.joints[(int)J::SPINE_01]    += roll_vec * 0.6f;
                pose.joints[(int)J::L_UPPER_LEG] += roll_vec;
                pose.joints[(int)J::R_UPPER_LEG] += roll_vec;

                // Apply vertical bob to hips and spine.
                pose.joints[(int)J::HIPS].z     += anim.hip_bob;
                pose.joints[(int)J::SPINE_01].z += anim.hip_bob * 0.5f;

                // ---- Acceleration-driven torso lean ----
                glm::vec3 cur_vel(vel.x, vel.y, 0.0f);
                glm::vec3 accel = (dt > 1e-6f) ? ((cur_vel - anim.prev_velocity) / dt)
                                               : glm::vec3(0.0f);
                anim.prev_velocity = cur_vel;

                float accel_len = glm::length(accel);
                const float max_lean = glm::radians(8.0f);
                float lean_factor = std::min(accel_len * 0.015f, max_lean);

                glm::vec3 lean_dir{0.0f};
                if (accel_len > 0.01f)
                    lean_dir = glm::normalize(accel);

                float target_lean_x = lean_dir.x * lean_factor;
                float target_lean_y = lean_dir.y * lean_factor;

                float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
                float speed_blend = std::min(1.0f, speed / gait.move_speed);
                float fwd_lean = speed_blend * glm::radians(2.5f);
                target_lean_x += fwd_x * fwd_lean;
                target_lean_y += fwd_y * fwd_lean;

                anim.chest_lean_x = smooth_damp(anim.chest_lean_x, target_lean_x,
                                                 &anim.chest_lean_x_rate, 0.05f, dt);
                anim.chest_lean_y = smooth_damp(anim.chest_lean_y, target_lean_y,
                                                 &anim.chest_lean_y_rate, 0.05f, dt);

                anim.neck_lean_x = smooth_damp(anim.neck_lean_x, anim.chest_lean_x * 0.7f,
                                               &anim.neck_lean_x_rate, 0.08f, dt);
                anim.neck_lean_y = smooth_damp(anim.neck_lean_y, anim.chest_lean_y * 0.7f,
                                               &anim.neck_lean_y_rate, 0.08f, dt);

                anim.head_lean_x = smooth_damp(anim.head_lean_x, anim.chest_lean_x * 0.5f,
                                               &anim.head_lean_x_rate, 0.12f, dt);
                anim.head_lean_y = smooth_damp(anim.head_lean_y, anim.chest_lean_y * 0.5f,
                                               &anim.head_lean_y_rate, 0.12f, dt);

                anim.lean_x = anim.chest_lean_x;
                anim.lean_y = anim.chest_lean_y;

                pose.joints[(int)J::CHEST] += glm::vec3(anim.chest_lean_x, anim.chest_lean_y, 0.0f);
                pose.joints[(int)J::NECK]  += glm::vec3(anim.neck_lean_x,  anim.neck_lean_y,  0.0f);
                pose.joints[(int)J::HEAD]  += glm::vec3(anim.head_lean_x,  anim.head_lean_y,  0.0f);

                // ---- Idle micro-motion ----
                float idle_blend = 1.0f - std::min(1.0f, speed / 0.2f);

                anim.breath_phase += dt * glm::two_pi<float>() * 0.6f;
                float breath_offset = sinf(anim.breath_phase) * 0.008f * idle_blend;
                pose.joints[(int)J::CHEST].z += breath_offset;
                pose.joints[(int)J::NECK].z  += breath_offset * 0.6f;
                pose.joints[(int)J::HEAD].z  += breath_offset * 0.4f;

                anim.idle_sway_phase += dt * glm::two_pi<float>() * 0.15f;
                float idle_sway = sinf(anim.idle_sway_phase) * 0.01f * idle_blend;
                glm::vec3 idle_sway_vec(rght_x * idle_sway, rght_y * idle_sway, 0.0f);
                pose.joints[(int)J::HIPS]     += idle_sway_vec;
                pose.joints[(int)J::SPINE_01] += idle_sway_vec;

                // ---- Idle weight shift ----
                if (idle_blend > 0.1f) {
                    anim.idle_weight_phase += dt * glm::two_pi<float>() * 0.3f;
                    float weight_shift = sinf(anim.idle_weight_phase);

                    float hip_shift = weight_shift * 0.04f * idle_blend;
                    glm::vec3 shift_vec(rght_x * hip_shift, rght_y * hip_shift, 0.0f);
                    pose.joints[(int)J::HIPS]        += shift_vec;
                    pose.joints[(int)J::SPINE_01]    += shift_vec * 0.7f;
                    pose.joints[(int)J::L_UPPER_LEG] += shift_vec;
                    pose.joints[(int)J::R_UPPER_LEG] += shift_vec;

                    float weight_dip = (1.0f - fabsf(weight_shift)) * 0.012f * idle_blend;
                    pose.joints[(int)J::HIPS].z     -= weight_dip;
                    pose.joints[(int)J::SPINE_01].z -= weight_dip * 0.5f;

                    glm::vec3 counter_vec = shift_vec * -0.3f;
                    pose.joints[(int)J::CHEST] += counter_vec;
                    pose.joints[(int)J::NECK]  += counter_vec * 0.5f;
                }

                // ---- Compute derived joints ----
                // ROOT: ground anchor below HIPS
                glm::vec3 hips = pose.joints[(int)J::HIPS];
                pose.joints[(int)J::ROOT] = glm::vec3(hips.x, hips.y,
                    hips.z - (cfg.leg_len + cfg.shin_len));

                // SPINE_02: mid-point between SPINE_01 and CHEST
                pose.joints[(int)J::SPINE_02] = glm::mix(
                    pose.joints[(int)J::SPINE_01],
                    pose.joints[(int)J::CHEST],
                    0.6f);

                // HEAD_END: skull-top nub
                pose.joints[(int)J::HEAD_END] = pose.joints[(int)J::HEAD]
                    + glm::vec3(0.0f, 0.0f, cfg.head_radius);

                // Clavicles: 25% from CHEST toward each upper arm
                pose.joints[(int)J::L_CLAVICLE] = glm::mix(
                    pose.joints[(int)J::CHEST],
                    pose.joints[(int)J::L_UPPER_ARM],
                    0.25f);
                pose.joints[(int)J::R_CLAVICLE] = glm::mix(
                    pose.joints[(int)J::CHEST],
                    pose.joints[(int)J::R_UPPER_ARM],
                    0.25f);

                // Toes: extend forward from foot
                glm::vec3 facing_dir(fwd_x, fwd_y, 0.0f);
                pose.joints[(int)J::L_TOE] = pose.joints[(int)J::L_FOOT]
                    + facing_dir * cfg.toe_len;
                pose.joints[(int)J::R_TOE] = pose.joints[(int)J::R_FOOT]
                    + facing_dir * cfg.toe_len;

                // Pole targets
                glm::vec3 fwd3(fwd_x, fwd_y, 0.0f);
                pose.joints[(int)J::POLE_KNEE_L]  = pose.joints[(int)J::L_LOWER_LEG] + fwd3 * 0.25f;
                pose.joints[(int)J::POLE_KNEE_R]  = pose.joints[(int)J::R_LOWER_LEG] + fwd3 * 0.25f;
                pose.joints[(int)J::POLE_ELBOW_L] = pose.joints[(int)J::L_LOWER_ARM] - fwd3 * 0.25f;
                pose.joints[(int)J::POLE_ELBOW_R] = pose.joints[(int)J::R_LOWER_ARM] - fwd3 * 0.25f;

                // IK goals = current end-effector positions
                pose.joints[(int)J::IK_FOOT_L] = pose.joints[(int)J::L_FOOT];
                pose.joints[(int)J::IK_FOOT_R] = pose.joints[(int)J::R_FOOT];
                pose.joints[(int)J::IK_HAND_L] = pose.joints[(int)J::L_HAND];
                pose.joints[(int)J::IK_HAND_R] = pose.joints[(int)J::R_HAND];

                (void)cfg;
            });
        });

    // =========================================================================
    // 6. AnimationLogSystem
    //    JSONL frame telemetry — runs after SkeletonFinaliseSystem.
    // =========================================================================
    ecs.system("AnimationLogSystem")
        .kind(flecs::PostUpdate)
        .run([&anim_log, &ecs, &camera, player_entity](flecs::iter &) {
            if (!anim_log.active) return;
            if (!player_entity.is_alive()) return;

            const auto *t    = player_entity.get<Transform>();
            const auto *vel  = player_entity.get<Velocity>();
            const auto *gait = player_entity.get<ProceduralGait>();
            const auto *legs = player_entity.get<LegState>();
            const auto *pose = player_entity.get<RigPose>();
            const auto *cfg  = player_entity.get<ActorConfig>();
            const auto *anim = player_entity.get<RigState>();
            if (!t || !vel || !gait || !legs || !pose || !cfg || !anim) return;

            float dt = ecs.delta_time();
            anim_log.begin_frame(dt);
            anim_log.log_transform(*t, *vel);
            anim_log.log_gait(*gait);
            anim_log.log_legs(*legs, *t, *cfg);
            anim_log.log_joints(*pose, *t);
            anim_log.log_finalize(anim->sway_phase, anim->sway_amount,
                                   anim->lean_x, anim->lean_y);
            anim_log.log_camera(camera);
            anim_log.log_dynamics(*anim, *vel, dt);
            anim_log.log_arm_swing(*anim);
            anim_log.log_grounding(*anim, *legs);
            anim_log.end_frame();
        });
}
