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

static const AnimationConfig s_anim_cfg{};

static void emit_strut(std::vector<BasaltVertex> &verts,
                       const glm::vec3 &pA, const glm::vec3 &pB,
                       float thickness) {
    glm::vec3 seg = pB - pA;
    float len = glm::length(seg);
    if (len < 1e-5f) return;
    glm::vec3 dir = seg / len;

    glm::vec3 right;
    if (fabsf(glm::dot(dir, glm::vec3(0.0f, 0.0f, 1.0f))) < 0.99f)
        right = glm::normalize(glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), dir));
    else
        right = glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), dir));
    glm::vec3 fwd = glm::cross(dir, right);

    constexpr int SIDES = 3;
    constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    glm::vec3 ring_A[SIDES], ring_B[SIDES];
    for (int i = 0; i < SIDES; ++i) {
        float angle = (float)i * (TWO_PI / (float)SIDES);
        glm::vec3 offset = (right * cosf(angle) + fwd * sinf(angle)) * thickness;
        ring_A[i] = pA + offset;
        ring_B[i] = pB + offset;
    }

    constexpr float cr = 0.0f, cg = 1.0f, cb = 1.0f;
    constexpr float sheen = 0.3f;

    auto push = [&](const glm::vec3 &pos, const glm::vec3 &n) {
        BasaltVertex bv;
        bv.pos_x   = pos.x;  bv.pos_y   = pos.y;  bv.pos_z   = pos.z;
        bv.color_r = cr;      bv.color_g = cg;      bv.color_b = cb;
        bv.sheen   = sheen;
        bv.nx = n.x; bv.ny = n.y; bv.nz = n.z;
        verts.push_back(bv);
    };

    for (int i = 0; i < SIDES; ++i) {
        int j = (i + 1) % SIDES;

        glm::vec3 p00 = ring_A[i];
        glm::vec3 p10 = ring_A[j];
        glm::vec3 p01 = ring_B[i];
        glm::vec3 p11 = ring_B[j];

        glm::vec3 cross_val = glm::cross(p10 - p00, p01 - p00);
        float cross_len = glm::length(cross_val);
        glm::vec3 face_n = (cross_len > 1e-7f) ? (cross_val / cross_len) : dir;

        push(p00, face_n); push(p10, face_n); push(p01, face_n);
        push(p10, face_n); push(p11, face_n); push(p01, face_n);
    }
}

static void append_bone_cage(std::vector<BasaltVertex> &verts,
                              const glm::mat4 &mat_A,
                              const glm::vec3 &pos_B,
                              float radius_A, float radius_B,
                              int segments, glm::vec4 color) {
    glm::vec3 pos_A      = glm::vec3(mat_A[3]);
    glm::vec3 bone_right = glm::vec3(mat_A[0]);
    glm::vec3 bone_fwd   = glm::vec3(mat_A[1]);

    glm::vec3 bone_vec = pos_B - pos_A;
    float bone_len = glm::length(bone_vec);
    if (bone_len < 1e-5f) return;
    (void)color;

    segments = std::max(segments, 3);
    segments = std::min(segments, 8);

    constexpr int MAX_SIDES = 8;
    constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    glm::vec3 ring_A[MAX_SIDES];
    glm::vec3 ring_B[MAX_SIDES];

    for (int i = 0; i < segments; ++i) {
        float angle = (float)i * (TWO_PI / (float)segments);
        float ca = cosf(angle), sa = sinf(angle);
        glm::vec3 dir = bone_right * ca + bone_fwd * sa;

        ring_A[i] = pos_A + dir * radius_A;
        ring_B[i] = pos_B + dir * radius_B;
    }

    constexpr float STRUT_THICKNESS = 0.003f;

    for (int i = 0; i < segments; ++i) {
        int j = (i + 1) % segments;
        emit_strut(verts, ring_A[i], ring_B[i], STRUT_THICKNESS);
        emit_strut(verts, ring_A[i], ring_A[j], STRUT_THICKNESS);
        emit_strut(verts, ring_B[i], ring_B[j], STRUT_THICKNESS);
    }
}

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

static float smooth_damp_angle(float current, float target, float *velocity,
                                float smooth_time, float dt) {
    float delta = target - current;
    while (delta >  glm::pi<float>()) delta -= glm::two_pi<float>();
    while (delta < -glm::pi<float>()) delta += glm::two_pi<float>();
    return smooth_damp(current, current + delta, velocity, smooth_time, dt);
}

static void solve_two_bone(glm::vec3 H, glm::vec3 target,
                           float a, float b,
                           glm::vec3 pole, glm::vec3 fallback_perp,
                           glm::vec3 &out_mid, glm::vec3 &out_end) {
    out_end = target;

    glm::vec3 axis = target - H;
    float D = glm::length(axis);
    float min_D = fabsf(a - b) + 0.001f;
    float max_D = a + b - 0.001f;

    float stretch_limit = max_D * 1.15f;
    if (D > stretch_limit && D > 1e-5f)
        out_end = H + (axis / D) * stretch_limit;

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
        perp = fallback_perp;

    glm::vec3 dir_to_mid = axis_n * cosf(alpha) + perp * sinf(alpha);
    out_mid = H + dir_to_mid * a;
}

void register_rig_systems(flecs::world &ecs,
                           InputSystem    &input,
                           CameraState    &camera,
                           AnimationLogger &anim_log,
                           flecs::entity   player_entity) {

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

            float raw_x = 0.0f, raw_y = 0.0f;
            if (in.held[(int)Action::MoveUp])    raw_y -= 1.0f;
            if (in.held[(int)Action::MoveDown])  raw_y += 1.0f;
            if (in.held[(int)Action::MoveLeft])  raw_x -= 1.0f;
            if (in.held[(int)Action::MoveRight]) raw_x += 1.0f;

            static constexpr float COS_ISO = 0.70710678118f;
            static constexpr float SIN_ISO = 0.70710678118f;
            float desired_x = ( raw_x * COS_ISO + raw_y * SIN_ISO) * gait->move_speed;
            float desired_y = (-raw_x * SIN_ISO + raw_y * COS_ISO) * gait->move_speed;

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

            float turn_delta = t->facing - anim->visual_facing;
            while (turn_delta >  glm::pi<float>()) turn_delta -= glm::two_pi<float>();
            while (turn_delta < -glm::pi<float>()) turn_delta += glm::two_pi<float>();

            anim->turn_delta     = turn_delta;
            anim->turn_magnitude = fabsf(turn_delta);
            anim->turn_urgency   = anim->turn_magnitude / glm::pi<float>();

            constexpr float TURN_ENTER_RAD = 1.0472f;
            constexpr float TURN_EXIT_RAD  = 0.3491f;
            if (!anim->in_large_turn && anim->turn_magnitude > TURN_ENTER_RAD)
                anim->in_large_turn = true;
            else if (anim->in_large_turn && anim->turn_magnitude < TURN_EXIT_RAD)
                anim->in_large_turn = false;

            constexpr float SMOOTH_MIN = 0.15f;
            constexpr float SMOOTH_MAX = 0.50f;
            float turn_t = std::clamp(anim->turn_urgency, 0.0f, 1.0f);
            float adaptive_smooth = SMOOTH_MIN + (SMOOTH_MAX - SMOOTH_MIN) * turn_t * turn_t;

            anim->visual_facing = smooth_damp_angle(anim->visual_facing, t->facing,
                                                     &anim->visual_facing_rate, adaptive_smooth, dt);

            float chest_smooth = (spd > 0.3f) ? 0.22f : 0.06f;
            anim->chest_facing = smooth_damp_angle(anim->chest_facing, anim->visual_facing,
                                                    &anim->chest_facing_rate, chest_smooth, dt);

            if (spd <= 0.001f) {
                anim->visual_facing_rate = 0.0f;
                anim->turn_delta     = 0.0f;
                anim->turn_magnitude = 0.0f;
                anim->turn_urgency   = 0.0f;
                anim->in_large_turn  = false;
                anim->chest_facing_rate = 0.0f;
            }

            auto *look = player_entity.get_mut<LookAtTarget>();
            if (look) {
                if (spd > 0.1f) {
                    look->position = glm::vec3(t->x + vel->x / spd * 5.0f,
                                               t->y + vel->y / spd * 5.0f,
                                               t->z);
                    look->active = true;
                    look->weight = std::min(1.0f, look->weight + ecs.delta_time() * 4.0f);
                } else {
                    look->weight = std::max(0.0f, look->weight - ecs.delta_time() * 2.0f);
                    if (look->weight < 0.01f) look->active = false;
                }
            }
        });

    ecs.system("ActorGroundingSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;

            ecs.each([&](ActorTag, Transform &t, const ActorConfig &,
                         const LegState *) {
                t.z = sample_world_height(*map_data, t.x, t.y);
            });
        });

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

                float turn_urgency = anim.turn_urgency;

                float vf_fwd_x = cosf(anim.visual_facing);
                float vf_fwd_y = sinf(anim.visual_facing);

                float turn_blend = turn_urgency * 0.7f;
                float step_dx = vel_dx * (1.0f - turn_blend) + vf_fwd_x * turn_blend;
                float step_dy = vel_dy * (1.0f - turn_blend) + vf_fwd_y * turn_blend;
                float step_len = sqrtf(step_dx * step_dx + step_dy * step_dy);
                if (step_len > 0.001f) { step_dx /= step_len; step_dy /= step_len; }
                else { step_dx = vf_fwd_x; step_dy = vf_fwd_y; }

                constexpr float SWING_RATE = 2.7f;

                float dir_scale = 1.0f;
                if (speed > 0.01f) {
                    float dx = vel.x / speed;
                    float dy = vel.y / speed;
                    float iso_align = fabsf(dx + dy) * 0.7071f;
                    dir_scale = 1.0f + iso_align * s_anim_cfg.directional_speed_scale;
                }
                gait.phase += speed * dt * SWING_RATE * dir_scale;

                (void)0;

                float step_rght_x = -step_dy, step_rght_y = step_dx;

                float hip_sign[2] = { -1.0f, 1.0f };

                float speed_ratio      = std::max(0.4f, std::min(1.0f, speed / gait.move_speed));
                float adaptive_duration = gait.step_duration / speed_ratio;
                adaptive_duration *= (1.0f - turn_urgency * 0.3f);

                float speed_factor = std::min(1.0f, speed / (gait.move_speed * 0.15f));
                speed_factor *= (1.0f - turn_urgency * 0.5f);

                if (turn_urgency > 0.4f && !legs.stepping[0] && !legs.stepping[1]) {
                    float behind[2];
                    for (int i = 0; i < 2; ++i) {
                        float fx = legs.foot[i].x - t.x;
                        float fy = legs.foot[i].y - t.y;
                        behind[i] = fx * step_dx + fy * step_dy;
                    }
                    legs.turn_step_queued = (behind[0] < behind[1]) ? 0 : 1;
                }
                if (turn_urgency <= 0.4f && legs.turn_step_queued >= 0
                    && !legs.stepping[0] && !legs.stepping[1]) {
                    legs.turn_step_queued = -1;
                }

                for (int leg = 0; leg < 2; ++leg) {
                    int other_leg = 1 - leg;

                    float hip_x = t.x + step_rght_x * hip_sign[leg] * cfg.hip_width;
                    float hip_y = t.y + step_rght_y * hip_sign[leg] * cfg.hip_width;

                    float half_stride = gait.stride_len * 0.5f;
                    float center_off = half_stride * speed_factor;
                    float center_x = hip_x + step_dx * center_off;
                    float center_y = hip_y + step_dy * center_off;

                    if (!legs.stepping[leg]) {
                        float dx   = legs.foot[leg].x - center_x;
                        float dy   = legs.foot[leg].y - center_y;
                        float dist = sqrtf(dx * dx + dy * dy);

                        bool other_planted = !legs.stepping[other_leg];
                        float trigger_dist = half_stride * (0.25f + 0.75f * speed_factor);
                        bool turn_blocked = (turn_urgency > 0.4f
                                             && legs.turn_step_queued >= 0
                                             && legs.turn_step_queued != leg);
                        if (dist > trigger_dist && other_planted && !turn_blocked) {
                            legs.stepping[leg]  = true;
                            legs.progress[leg]  = 0.0f;
                            legs.prev_foot[leg] = legs.foot[leg];
                            if (legs.turn_step_queued == leg)
                                legs.turn_step_queued = -1;

                            float step_travel = speed * adaptive_duration;
                            float target_off  = (half_stride + step_travel * 0.75f) * speed_factor;
                            float tgt_x = hip_x + step_dx * target_off;
                            float tgt_y = hip_y + step_dy * target_off;

                            float tgt_lat = (tgt_x - t.x) * step_rght_x + (tgt_y - t.y) * step_rght_y;
                            if ((hip_sign[leg] < 0 && tgt_lat > 0.02f) || (hip_sign[leg] > 0 && tgt_lat < -0.02f)) {
                                tgt_x -= step_rght_x * (tgt_lat - hip_sign[leg] * 0.05f);
                                tgt_y -= step_rght_y * (tgt_lat - hip_sign[leg] * 0.05f);
                            }

                            float tgt_z = sphere_trace_height(*map_data, tgt_x, tgt_y, cfg.leg_radius);
                            legs.target[leg]    = {tgt_x, tgt_y, tgt_z};
                        }
                    }

                    if (legs.stepping[leg]) {
                        legs.progress[leg] += dt / adaptive_duration;
                        float progress = std::min(legs.progress[leg], 1.0f);

                        float warped_t = progress * progress * progress
                                         * (progress * (progress * 6.0f - 15.0f) + 10.0f);

                        float ts_z  = progress * progress * (3.0f - 2.0f * progress);

                        legs.foot[leg].x = legs.prev_foot[leg].x + (legs.target[leg].x - legs.prev_foot[leg].x) * warped_t;
                        legs.foot[leg].y = legs.prev_foot[leg].y + (legs.target[leg].y - legs.prev_foot[leg].y) * warped_t;
                        legs.foot[leg].z = legs.prev_foot[leg].z + (legs.target[leg].z - legs.prev_foot[leg].z) * ts_z
                                           + sinf(progress * glm::pi<float>()) * gait.step_height;

                        if (legs.progress[leg] >= 1.0f) {
                            float step_dist = glm::length(legs.target[leg] - legs.prev_foot[leg]);
                            anim.foot_contact_velocity[leg] = step_dist / adaptive_duration;

                            legs.stepping[leg] = false;
                            legs.foot[leg]     = legs.target[leg];
                            legs.plant_pos[leg] = legs.target[leg];
                            legs.planted[leg]   = true;
                        }
                    }
                }

                for (int leg = 0; leg < 2; ++leg) {
                    if (!legs.stepping[leg]) {
                        float ground_z = sphere_trace_height(*map_data,
                                            legs.foot[leg].x, legs.foot[leg].y, cfg.leg_radius);
                        legs.foot[leg].z = ground_z;
                    }
                }

                for (int leg = 0; leg < 2; ++leg) {
                    if (legs.stepping[leg] || legs.stepping[1 - leg]) continue;
                    float lat = (legs.foot[leg].x - t.x) * step_rght_x
                              + (legs.foot[leg].y - t.y) * step_rght_y;
                    bool crossed = (hip_sign[leg] < 0) ? (lat > 0.02f) : (lat < -0.02f);
                    if (crossed) {
                        legs.stepping[leg] = true;
                        legs.progress[leg] = 0.0f;
                        legs.prev_foot[leg] = legs.foot[leg];
                        float c_hip_x = t.x + step_rght_x * hip_sign[leg] * cfg.hip_width;
                        float c_hip_y = t.y + step_rght_y * hip_sign[leg] * cfg.hip_width;
                        float step_travel = speed * adaptive_duration;
                        float target_off  = (gait.stride_len * 0.5f + step_travel * 0.75f) * speed_factor;
                        float tgt_x = c_hip_x + step_dx * target_off;
                        float tgt_y = c_hip_y + step_dy * target_off;
                        legs.target[leg] = {tgt_x, tgt_y,
                            sphere_trace_height(*map_data, tgt_x, tgt_y, cfg.leg_radius)};
                    }
                }

                float max_horiz = gait.stride_len * 0.9f;
                for (int leg = 0; leg < 2; ++leg) {
                    float hip_x = t.x + step_rght_x * hip_sign[leg] * cfg.hip_width;
                    float hip_y = t.y + step_rght_y * hip_sign[leg] * cfg.hip_width;

                    if (!legs.stepping[leg]) {
                        if (legs.planted[leg]) {
                            legs.foot[leg].x = legs.plant_pos[leg].x;
                            legs.foot[leg].y = legs.plant_pos[leg].y;
                        }
                        float dx = legs.foot[leg].x - hip_x;
                        float dy = legs.foot[leg].y - hip_y;
                        float hd = sqrtf(dx * dx + dy * dy);
                        if (hd > max_horiz) {
                            if (!legs.stepping[1 - leg]) {
                                legs.stepping[leg]  = true;
                                legs.progress[leg]  = 0.0f;
                                legs.prev_foot[leg] = legs.foot[leg];
                                float step_travel = speed * adaptive_duration;
                                float target_off  = (gait.stride_len * 0.5f + step_travel * 0.75f) * speed_factor;
                                float tgt_x = hip_x + step_dx * target_off;
                                float tgt_y = hip_y + step_dy * target_off;
                                legs.target[leg] = {tgt_x, tgt_y,
                                    sphere_trace_height(*map_data, tgt_x, tgt_y, cfg.leg_radius)};
                            } else {
                                float s = max_horiz / hd;
                                legs.foot[leg].x = hip_x + dx * s;
                                legs.foot[leg].y = hip_y + dy * s;
                                legs.plant_pos[leg] = legs.foot[leg];
                            }
                        }
                    } else if (legs.progress[leg] < 0.4f) {
                        float tdx = legs.target[leg].x - hip_x;
                        float tdy = legs.target[leg].y - hip_y;
                        float th  = sqrtf(tdx * tdx + tdy * tdy);
                        if (th > max_horiz) {
                            float step_travel = speed * adaptive_duration;
                            float target_off  = (gait.stride_len * 0.5f
                                                + step_travel * 0.75f) * speed_factor;
                            legs.target[leg].x = hip_x + step_dx * target_off;
                            legs.target[leg].y = hip_y + step_dy * target_off;
                            legs.target[leg].z = sphere_trace_height(*map_data,
                                                    legs.target[leg].x,
                                                    legs.target[leg].y, cfg.leg_radius);
                        }
                    }
                }
            });
        });

    ecs.system("GrabDriveSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([&](ActorTag,
                         const GrabState       &grab,
                         const ActorConfig     &cfg,
                         ArmIKGoal             &arm_ik,
                         ProceduralGait        &gait,
                         AnimationOverlay      *overlay) {

                if (grab.active_l) {
                    arm_ik.target_l = grab.grab_point;
                    arm_ik.weight_l = grab.weight;
                } else {
                    arm_ik.weight_l = 0.0f;
                }
                if (grab.active_r) {
                    arm_ik.target_r = grab.grab_point;
                    arm_ik.weight_r = grab.weight;
                } else {
                    arm_ik.weight_r = 0.0f;
                }

                float carry_w = std::max(grab.active_l ? grab.weight : 0.0f,
                                         grab.active_r ? grab.weight : 0.0f);
                if (carry_w > 0.01f) {
                    if (overlay) {
                        overlay->active = AnimationOverlay::Type::HeavyCarry;
                        overlay->intensity = carry_w;
                    }
                }
            });
        });

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
                         const ArmIKGoal  *arm_ik,
                         RigState         &anim,
                         RigPose          &pose) {

                using J = Joint;

                float facing = anim.visual_facing;
                float fwd_x  =  cosf(facing), fwd_y  = sinf(facing);
                float rght_x = -sinf(facing), rght_y = cosf(facing);

                float chest_fwd_x  =  cosf(anim.chest_facing), chest_fwd_y  = sinf(anim.chest_facing);
                float chest_rght_x = -sinf(anim.chest_facing), chest_rght_y = cosf(anim.chest_facing);

                float leg_height = cfg.leg_len + cfg.shin_len;
                glm::vec3 hips(t.x, t.y, t.z + leg_height);
                glm::vec3 spine01 = hips  + glm::vec3(0, 0, cfg.torso_len * 0.4f);
                glm::vec3 chest   = hips  + glm::vec3(0, 0, cfg.torso_len);
                glm::vec3 neck    = chest + glm::vec3(0, 0, cfg.neck_len);
                glm::vec3 head    = neck  + glm::vec3(0, 0, cfg.head_radius);

                pose.joints[(int)J::HIPS]     = hips;
                pose.joints[(int)J::SPINE_01] = spine01;
                pose.joints[(int)J::CHEST]    = chest;
                pose.joints[(int)J::NECK]     = neck;
                pose.joints[(int)J::HEAD]     = head;

                float center_hip_z = t.z + leg_height;
                float l_hip_z = center_hip_z;
                float r_hip_z = center_hip_z;
                if (legs.foot[0].z != 0.0f || legs.foot[1].z != 0.0f) {
                    float l_ground_hip = legs.foot[0].z + leg_height;
                    float r_ground_hip = legs.foot[1].z + leg_height;
                    l_hip_z = center_hip_z * 0.7f + l_ground_hip * 0.3f;
                    r_hip_z = center_hip_z * 0.7f + r_ground_hip * 0.3f;
                }
                glm::vec3 l_upper_leg(t.x - rght_x * cfg.hip_width, t.y - rght_y * cfg.hip_width, l_hip_z);
                glm::vec3 r_upper_leg(t.x + rght_x * cfg.hip_width, t.y + rght_y * cfg.hip_width, r_hip_z);
                pose.joints[(int)J::L_UPPER_LEG] = l_upper_leg;
                pose.joints[(int)J::R_UPPER_LEG] = r_upper_leg;

                glm::vec3 l_upper_arm(chest.x - chest_rght_x * cfg.shoulder_width,
                                      chest.y - chest_rght_y * cfg.shoulder_width,
                                      chest.z);
                glm::vec3 r_upper_arm(chest.x + chest_rght_x * cfg.shoulder_width,
                                      chest.y + chest_rght_y * cfg.shoulder_width,
                                      chest.z);
                pose.joints[(int)J::L_UPPER_ARM] = l_upper_arm;
                pose.joints[(int)J::R_UPPER_ARM] = r_upper_arm;

                float speed       = sqrtf(vel.x * vel.x + vel.y * vel.y);
                float swing_amp   = std::min(1.0f, speed / gait.move_speed) * glm::radians(30.0f);

                float l_arm_target = sinf(gait.phase + glm::pi<float>()) * swing_amp;
                float r_arm_target = sinf(gait.phase)                      * swing_amp;
                anim.l_arm_target = l_arm_target;
                anim.r_arm_target = r_arm_target;

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

                glm::vec3 right_axis(chest_rght_x, chest_rght_y, 0.0f);
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

                glm::vec3 l_lower_arm_fk = swing_elbow_pos(l_upper_arm, anim.l_shoulder_smooth,
                                                            anim.l_elbow_smooth, cfg.arm_len);
                glm::vec3 l_hand_fk      = swing_wrist_pos(l_lower_arm_fk, anim.l_shoulder_smooth,
                                                            anim.l_wrist_smooth, cfg.forearm_len);

                glm::vec3 r_lower_arm_fk = swing_elbow_pos(r_upper_arm, anim.r_shoulder_smooth,
                                                            anim.r_elbow_smooth, cfg.arm_len);
                glm::vec3 r_hand_fk      = swing_wrist_pos(r_lower_arm_fk, anim.r_shoulder_smooth,
                                                            anim.r_wrist_smooth, cfg.forearm_len);

                glm::vec3 l_lower_arm = l_lower_arm_fk, l_hand = l_hand_fk;
                glm::vec3 r_lower_arm = r_lower_arm_fk, r_hand = r_hand_fk;

                if (arm_ik) {
                    glm::vec3 fwd3_chest(chest_fwd_x, chest_fwd_y, 0.0f);

                    if (arm_ik->weight_l > 0.001f) {
                        glm::vec3 l_elbow_pole = l_upper_arm - fwd3_chest * 0.3f + glm::vec3(0,0,-0.1f);
                        glm::vec3 l_elbow_ik, l_hand_ik;
                        solve_two_bone(l_upper_arm, arm_ik->target_l,
                                       cfg.arm_len, cfg.forearm_len,
                                       l_elbow_pole, glm::vec3(-chest_rght_x, -chest_rght_y, 0.0f),
                                       l_elbow_ik, l_hand_ik);
                        float w = arm_ik->weight_l;
                        l_lower_arm = glm::mix(l_lower_arm_fk, l_elbow_ik, w);
                        l_hand      = glm::mix(l_hand_fk, l_hand_ik, w);
                    }
                    if (arm_ik->weight_r > 0.001f) {
                        glm::vec3 r_elbow_pole = r_upper_arm - fwd3_chest * 0.3f + glm::vec3(0,0,-0.1f);
                        glm::vec3 r_elbow_ik, r_hand_ik;
                        solve_two_bone(r_upper_arm, arm_ik->target_r,
                                       cfg.arm_len, cfg.forearm_len,
                                       r_elbow_pole, glm::vec3(chest_rght_x, chest_rght_y, 0.0f),
                                       r_elbow_ik, r_hand_ik);
                        float w = arm_ik->weight_r;
                        r_lower_arm = glm::mix(r_lower_arm_fk, r_elbow_ik, w);
                        r_hand      = glm::mix(r_hand_fk, r_hand_ik, w);
                    }
                }

                pose.joints[(int)J::L_LOWER_ARM] = l_lower_arm;
                pose.joints[(int)J::L_HAND]      = l_hand;
                pose.joints[(int)J::R_LOWER_ARM] = r_lower_arm;
                pose.joints[(int)J::R_HAND]      = r_hand;
                glm::vec3 l_fallback(-rght_x, -rght_y, 0.0f);
                glm::vec3 l_pole = l_upper_leg + glm::vec3(fwd_x * 0.5f - rght_x * 0.08f,
                                                             fwd_y * 0.5f - rght_y * 0.08f, 0.2f);
                glm::vec3 l_lower_leg, l_foot;
                solve_two_bone(l_upper_leg, legs.foot[0], cfg.leg_len, cfg.shin_len,
                               l_pole, l_fallback, l_lower_leg, l_foot);
                pose.joints[(int)J::L_LOWER_LEG] = l_lower_leg;
                pose.joints[(int)J::L_FOOT]      = l_foot;

                glm::vec3 r_fallback(rght_x, rght_y, 0.0f);
                glm::vec3 r_pole = r_upper_leg + glm::vec3(fwd_x * 0.5f + rght_x * 0.08f,
                                                             fwd_y * 0.5f + rght_y * 0.08f, 0.2f);
                glm::vec3 r_lower_leg, r_foot;
                solve_two_bone(r_upper_leg, legs.foot[1], cfg.leg_len, cfg.shin_len,
                               r_pole, r_fallback, r_lower_leg, r_foot);
                pose.joints[(int)J::R_LOWER_LEG] = r_lower_leg;
                pose.joints[(int)J::R_FOOT]      = r_foot;
            });
        });

    ecs.system("OverlaySystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform      &t,
                         const Velocity       &vel,
                         const ActorConfig    &cfg,
                         const ProceduralGait &gait,
                         AnimationOverlay     &overlay,
                         RigPose              &pose) {

                using J = Joint;
                using OT = AnimationOverlay::Type;

                if (overlay.active == OT::None || overlay.intensity < 0.001f) return;

                overlay.phase += dt * 2.0f;

                float I = overlay.intensity;
                float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                switch (overlay.active) {
                case OT::Limp: {
                    float limp_drop = sinf(gait.phase) * 0.04f * I;
                    pose.joints[(int)J::L_UPPER_LEG].z -= limp_drop;
                    pose.joints[(int)J::HIPS].z -= fabsf(limp_drop) * 0.3f;
                    break;
                }
                case OT::Fatigue: {
                    float fatigue_sway = sinf(overlay.phase * 0.8f) * 0.025f * I;
                    glm::vec3 sway_vec(rght_x * fatigue_sway, rght_y * fatigue_sway, 0.0f);
                    pose.joints[(int)J::CHEST]    += sway_vec;
                    pose.joints[(int)J::NECK]     += sway_vec * 1.2f;
                    pose.joints[(int)J::HEAD]     += sway_vec * 1.4f;
                    float lean = 0.03f * I;
                    pose.joints[(int)J::CHEST] += glm::vec3(fwd_x * lean, fwd_y * lean, 0.0f);
                    pose.joints[(int)J::NECK]  += glm::vec3(fwd_x * lean * 1.3f, fwd_y * lean * 1.3f, 0.0f);
                    break;
                }
                case OT::HeavyCarry: {
                    float lean = 0.05f * I;
                    pose.joints[(int)J::CHEST] += glm::vec3(fwd_x * lean, fwd_y * lean, 0.0f);
                    pose.joints[(int)J::NECK]  += glm::vec3(fwd_x * lean * 0.8f, fwd_y * lean * 0.8f, 0.0f);
                    pose.joints[(int)J::L_HAND].z -= 0.06f * I;
                    pose.joints[(int)J::R_HAND].z -= 0.06f * I;
                    break;
                }
                case OT::None: break;
                }
            });
        });

    ecs.system("LookAtSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform    &t,
                         const LookAtTarget &look,
                         RigState           &anim,
                         RigPose            &pose) {

                using J = Joint;

                if (!look.active && anim.look_yaw == 0.0f && anim.look_pitch == 0.0f)
                    return;

                glm::vec3 head_pos = pose.joints[(int)J::HEAD];
                glm::vec3 to_target = look.position - head_pos;
                float horiz_dist = sqrtf(to_target.x * to_target.x + to_target.y * to_target.y);

                float target_yaw = 0.0f, target_pitch = 0.0f;
                if (horiz_dist > 0.01f) {
                    float abs_yaw = atan2f(to_target.y, to_target.x);
                    target_yaw = abs_yaw - t.facing;
                    while (target_yaw >  glm::pi<float>()) target_yaw -= glm::two_pi<float>();
                    while (target_yaw < -glm::pi<float>()) target_yaw += glm::two_pi<float>();
                    target_pitch = atan2f(to_target.z, horiz_dist);
                }

                float max_yaw   = glm::radians(70.0f);
                float max_pitch = glm::radians(30.0f);
                target_yaw   = std::clamp(target_yaw,   -max_yaw,   max_yaw);
                target_pitch = std::clamp(target_pitch, -max_pitch, max_pitch);

                target_yaw   *= look.weight;
                target_pitch *= look.weight;

                anim.look_yaw = smooth_damp(anim.look_yaw, target_yaw,
                                             &anim.look_yaw_rate, 0.08f, dt);
                anim.look_pitch = smooth_damp(anim.look_pitch, target_pitch,
                                               &anim.look_pitch_rate, 0.08f, dt);

                float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
                float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

                auto apply_yaw = [&](Joint j, float frac) {
                    float offset = sinf(anim.look_yaw * frac) * 0.1f;
                    pose.joints[(int)j] += glm::vec3(rght_x * offset, rght_y * offset, 0.0f);
                };
                auto apply_pitch = [&](Joint j, float frac) {
                    pose.joints[(int)j].z += sinf(anim.look_pitch * frac) * 0.05f;
                };

                apply_yaw(J::HEAD, 0.6f);   apply_pitch(J::HEAD, 0.6f);
                apply_yaw(J::NECK, 0.3f);   apply_pitch(J::NECK, 0.3f);
                apply_yaw(J::CHEST, 0.1f);  apply_pitch(J::CHEST, 0.1f);
            });
        });

    ecs.system("SkeletonFinaliseSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            float dt = ecs.delta_time();
            ecs.each([&](ActorTag,
                         const Transform      &t,
                         const Velocity       &vel,
                         const ActorConfig    &cfg,
                         const ProceduralGait &gait,
                         const LegState       *legs,
                         RigState             &anim,
                         RigPose              &pose) {

                using J = Joint;

                float speed   = sqrtf(vel.x * vel.x + vel.y * vel.y);
                float rght_x  = -sinf(anim.visual_facing), rght_y = cosf(anim.visual_facing);

                if (legs) {
                    float foot_diff = legs->foot[0].z - legs->foot[1].z;
                    float max_tilt = glm::radians(8.0f);
                    float hip_tilt_target = std::clamp(
                        atan2f(foot_diff, cfg.hip_width * 2.0f),
                        -max_tilt, max_tilt);
                    anim.hip_tilt = smooth_damp(anim.hip_tilt, hip_tilt_target,
                                                &anim.hip_tilt_rate, 0.06f, dt);

                    float tilt_offset = sinf(anim.hip_tilt) * cfg.hip_width;
                    pose.joints[(int)J::L_UPPER_LEG].z -= tilt_offset;
                    pose.joints[(int)J::R_UPPER_LEG].z += tilt_offset;
                    pose.joints[(int)J::HIPS].z += sinf(anim.hip_tilt) * 0.5f * cfg.hip_width
                                                   * (foot_diff > 0 ? 1.0f : -1.0f) * 0.1f;
                }

                float balance_target = 0.0f;
                if (legs) {
                    if (legs->stepping[0] && !legs->stepping[1]) balance_target =  1.0f;
                    else if (legs->stepping[1] && !legs->stepping[0]) balance_target = -1.0f;
                }
                anim.support_balance = smooth_damp(anim.support_balance, balance_target,
                                                    &anim.support_balance_rate, 0.08f, dt);
                float sway = anim.support_balance * 0.04f;
                glm::vec3 sway_vec(rght_x * sway, rght_y * sway, 0.0f);
                pose.joints[(int)J::HIPS]     += sway_vec;
                pose.joints[(int)J::SPINE_01] += sway_vec;
                pose.joints[(int)J::CHEST]    += sway_vec;
                pose.joints[(int)J::NECK]     += sway_vec;
                pose.joints[(int)J::HEAD]     += sway_vec;

                float walk_blend = std::min(1.0f, speed / (gait.move_speed * 0.3f));

                float turn_urgency = std::min(1.0f, fabsf(anim.visual_facing_rate) / 10.0f);

                float target_hip_roll = sinf(gait.phase) * 0.06f * walk_blend;
                anim.hip_roll = smooth_damp(anim.hip_roll, target_hip_roll,
                                            &anim.hip_roll_rate, 0.04f, dt);

                float bob_blend = walk_blend * (1.0f - turn_urgency * 0.7f);
                float target_hip_bob = fabsf(sinf(gait.phase)) * 0.018f * bob_blend;
                anim.hip_bob = smooth_damp(anim.hip_bob, target_hip_bob,
                                           &anim.hip_bob_rate, 0.03f, dt);

                float step_dip_target = 0.0f;
                if (legs) {
                    float leg_height_dip = cfg.leg_len + cfg.shin_len;
                    float base_peak = leg_height_dip * 0.07f;
                    float turn_amp = 1.0f + turn_urgency * 0.5f;
                    float peak = base_peak * turn_amp * walk_blend;

                    for (int i = 0; i < 2; ++i) {
                        if (legs->stepping[i]) {
                            float p = legs->progress[i];
                            float dip = peak * 4.0f * p * (1.0f - p);
                            step_dip_target = std::max(step_dip_target, dip);
                        }
                    }
                }
                anim.hip_dip = smooth_damp(anim.hip_dip, step_dip_target,
                                           &anim.hip_dip_rate, 0.025f, dt);

                float roll_shift = sinf(anim.hip_roll) * cfg.hip_width * 0.4f;
                glm::vec3 roll_vec(rght_x * roll_shift, rght_y * roll_shift, 0.0f);
                pose.joints[(int)J::HIPS]        += roll_vec;
                pose.joints[(int)J::SPINE_01]    += roll_vec * 0.6f;
                pose.joints[(int)J::L_UPPER_LEG] += roll_vec;
                pose.joints[(int)J::R_UPPER_LEG] += roll_vec;

                float hip_z_offset = anim.hip_bob - anim.hip_dip;
                pose.joints[(int)J::HIPS].z         += hip_z_offset;
                pose.joints[(int)J::SPINE_01].z     += hip_z_offset * 0.7f;
                pose.joints[(int)J::L_UPPER_LEG].z  += hip_z_offset;
                pose.joints[(int)J::R_UPPER_LEG].z  += hip_z_offset;

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

                float fwd_x = cosf(anim.visual_facing), fwd_y = sinf(anim.visual_facing);
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

                {
                    float lag_angle = anim.visual_facing - anim.chest_facing;
                    while (lag_angle >  glm::pi<float>()) lag_angle -= glm::two_pi<float>();
                    while (lag_angle < -glm::pi<float>()) lag_angle += glm::two_pi<float>();

                    float lag_offset = sinf(lag_angle) * cfg.torso_len * 0.25f;
                    glm::vec3 lag_vec(rght_x * lag_offset, rght_y * lag_offset, 0.0f);

                    pose.joints[(int)J::CHEST]       += lag_vec;
                    pose.joints[(int)J::NECK]        += lag_vec * 0.7f;
                    pose.joints[(int)J::HEAD]        += lag_vec * 0.5f;
                    pose.joints[(int)J::L_UPPER_ARM] += lag_vec;
                    pose.joints[(int)J::R_UPPER_ARM] += lag_vec;
                }

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
                pose.joints[(int)J::NECK]     += idle_sway_vec;
                pose.joints[(int)J::HEAD]     += idle_sway_vec;

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

                pose.joints[(int)J::ROOT] = glm::vec3(t.x, t.y, t.z);

                pose.joints[(int)J::SPINE_02] = glm::mix(
                    pose.joints[(int)J::SPINE_01],
                    pose.joints[(int)J::CHEST],
                    0.6f);

                pose.joints[(int)J::HEAD_END] = pose.joints[(int)J::HEAD]
                    + glm::vec3(0.0f, 0.0f, cfg.head_radius);

                pose.joints[(int)J::L_CLAVICLE] = glm::mix(
                    pose.joints[(int)J::CHEST],
                    pose.joints[(int)J::L_UPPER_ARM],
                    0.25f);
                pose.joints[(int)J::R_CLAVICLE] = glm::mix(
                    pose.joints[(int)J::CHEST],
                    pose.joints[(int)J::R_UPPER_ARM],
                    0.25f);

                glm::vec3 facing_dir(fwd_x, fwd_y, 0.0f);
                pose.joints[(int)J::L_TOE] = pose.joints[(int)J::L_FOOT]
                    + facing_dir * cfg.toe_len;
                pose.joints[(int)J::R_TOE] = pose.joints[(int)J::R_FOOT]
                    + facing_dir * cfg.toe_len;

                glm::vec3 fwd3(fwd_x, fwd_y, 0.0f);
                pose.joints[(int)J::POLE_KNEE_L]  = pose.joints[(int)J::L_LOWER_LEG] + fwd3 * 0.25f;
                pose.joints[(int)J::POLE_KNEE_R]  = pose.joints[(int)J::R_LOWER_LEG] + fwd3 * 0.25f;

                float cf_x = cosf(anim.chest_facing), cf_y = sinf(anim.chest_facing);
                glm::vec3 chest_fwd3(cf_x, cf_y, 0.0f);
                pose.joints[(int)J::POLE_ELBOW_L] = pose.joints[(int)J::L_LOWER_ARM] - chest_fwd3 * 0.25f;
                pose.joints[(int)J::POLE_ELBOW_R] = pose.joints[(int)J::R_LOWER_ARM] - chest_fwd3 * 0.25f;

                pose.joints[(int)J::IK_FOOT_L] = pose.joints[(int)J::L_FOOT];
                pose.joints[(int)J::IK_FOOT_R] = pose.joints[(int)J::R_FOOT];
                pose.joints[(int)J::IK_HAND_L] = pose.joints[(int)J::L_HAND];
                pose.joints[(int)J::IK_HAND_R] = pose.joints[(int)J::R_HAND];

                (void)cfg;
            });
        });

    ecs.system("RigTransformSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([](ActorTag,
                        const ActorConfig    &cfg,
                        const RigState       &anim,
                        const RigPose        &pose,
                        RigTransforms        &xforms) {
                using J = Joint;
                const auto &jt = pose.joints;

                float vf = anim.visual_facing;
                glm::vec3 vis_fwd(cosf(vf), sinf(vf), 0.0f);
                glm::vec3 vis_rght(-sinf(vf), cosf(vf), 0.0f);

                float cf = anim.chest_facing;
                glm::vec3 chest_fwd(cosf(cf), sinf(cf), 0.0f);
                glm::vec3 chest_rght(-sinf(cf), cosf(cf), 0.0f);

                xforms.bones[(int)J::ROOT] = make_bone_mat4(
                    vis_rght, vis_fwd, glm::vec3(0.0f, 0.0f, 1.0f),
                    jt[(int)J::ROOT]);

                struct SpineEntry { Joint self; Joint child; float chest_blend; };
                static constexpr SpineEntry spine_chain[] = {
                    { J::HIPS,     J::SPINE_01, 0.0f },
                    { J::SPINE_01, J::SPINE_02, 0.0f },
                    { J::SPINE_02, J::CHEST,    0.5f },
                    { J::CHEST,    J::NECK,     1.0f },
                    { J::NECK,     J::HEAD,     1.0f },
                };
                for (const auto &e : spine_chain) {
                    glm::vec3 bone_dir = jt[(int)e.child] - jt[(int)e.self];
                    float blen = glm::length(bone_dir);
                    if (blen < 1e-5f) {
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)e.self], 1.0f);
                        xforms.bones[(int)e.self] = m;
                        continue;
                    }
                    glm::vec3 ref = glm::mix(vis_fwd, chest_fwd, e.chest_blend);
                    float ref_len = glm::length(ref);
                    if (ref_len < 1e-5f) ref = vis_fwd;
                    else ref /= ref_len;

                    glm::vec3 right, fwd, up;
                    build_bone_basis(bone_dir, ref, right, fwd, up);
                    xforms.bones[(int)e.self] = make_bone_mat4(right, fwd, up, jt[(int)e.self]);
                }
                {
                    glm::vec3 bone_dir = jt[(int)J::HEAD_END] - jt[(int)J::HEAD];
                    float blen = glm::length(bone_dir);
                    if (blen < 1e-5f) {
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)J::HEAD], 1.0f);
                        xforms.bones[(int)J::HEAD] = m;
                    } else {
                        glm::vec3 right, fwd, up;
                        build_bone_basis(bone_dir, chest_fwd, right, fwd, up);
                        xforms.bones[(int)J::HEAD] = make_bone_mat4(right, fwd, up, jt[(int)J::HEAD]);
                    }
                }
                {
                    glm::mat4 m = xforms.bones[(int)J::HEAD];
                    m[3] = glm::vec4(jt[(int)J::HEAD_END], 1.0f);
                    xforms.bones[(int)J::HEAD_END] = m;
                }

                auto compute_limb_chain = [&](Joint upper, Joint lower, Joint end,
                                              Joint toe, const glm::vec3 &fallback_perp,
                                              bool is_leg) {
                    glm::vec3 upper_dir = jt[(int)lower] - jt[(int)upper];
                    glm::vec3 lower_dir = jt[(int)end]   - jt[(int)lower];
                    float upper_len = glm::length(upper_dir);
                    float lower_len = glm::length(lower_dir);

                    glm::vec3 bend_normal = fallback_perp;
                    if (upper_len > 1e-5f && lower_len > 1e-5f) {
                        glm::vec3 bn = glm::cross(
                            upper_dir / upper_len, lower_dir / lower_len);
                        float bn_len = glm::length(bn);
                        if (bn_len > 1e-5f) bend_normal = bn / bn_len;
                    }

                    if (upper_len > 1e-5f) {
                        glm::vec3 up = upper_dir / upper_len;
                        glm::vec3 right = bend_normal;
                        glm::vec3 fwd = glm::cross(up, right);
                        right = glm::cross(fwd, up);
                        xforms.bones[(int)upper] = make_bone_mat4(right, fwd, up, jt[(int)upper]);
                    } else {
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)upper], 1.0f);
                        xforms.bones[(int)upper] = m;
                    }

                    if (lower_len > 1e-5f) {
                        glm::vec3 up = lower_dir / lower_len;
                        glm::vec3 right = bend_normal - glm::dot(bend_normal, up) * up;
                        float rlen = glm::length(right);
                        if (rlen < 1e-5f) {
                            right = fallback_perp - glm::dot(fallback_perp, up) * up;
                            rlen = glm::length(right);
                            if (rlen < 1e-5f) {
                                glm::vec3 alt = (std::abs(up.z) < 0.9f)
                                    ? glm::vec3(0.0f, 0.0f, 1.0f)
                                    : glm::vec3(1.0f, 0.0f, 0.0f);
                                right = glm::normalize(glm::cross(alt, up));
                                rlen = 1.0f;
                            }
                        }
                        right /= rlen;
                        glm::vec3 fwd = glm::cross(up, right);
                        xforms.bones[(int)lower] = make_bone_mat4(right, fwd, up, jt[(int)lower]);
                    } else {
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)lower], 1.0f);
                        xforms.bones[(int)lower] = m;
                    }

                    if (is_leg) {
                        glm::vec3 toe_dir = jt[(int)toe] - jt[(int)end];
                        float toe_len = glm::length(toe_dir);
                        glm::vec3 fwd = (toe_len > 1e-5f)
                            ? toe_dir / toe_len : vis_fwd;
                        glm::vec3 up(0.0f, 0.0f, 1.0f);
                        glm::vec3 right = glm::cross(fwd, up);
                        float rlen = glm::length(right);
                        if (rlen < 1e-5f) right = vis_rght;
                        else right /= rlen;
                        up = glm::cross(right, fwd);
                        xforms.bones[(int)end] = make_bone_mat4(right, fwd, up, jt[(int)end]);
                        glm::mat4 m = xforms.bones[(int)end];
                        m[3] = glm::vec4(jt[(int)toe], 1.0f);
                        xforms.bones[(int)toe] = m;
                    } else {
                        if (lower_len > 1e-5f) {
                            glm::mat4 m = xforms.bones[(int)lower];
                            m[3] = glm::vec4(jt[(int)end], 1.0f);
                            xforms.bones[(int)end] = m;
                        } else {
                            glm::mat4 m(1.0f);
                            m[3] = glm::vec4(jt[(int)end], 1.0f);
                            xforms.bones[(int)end] = m;
                        }
                    }
                };

                compute_limb_chain(J::L_UPPER_LEG, J::L_LOWER_LEG, J::L_FOOT, J::L_TOE,
                    glm::vec3(-vis_rght.x, -vis_rght.y, 0.0f), true);
                compute_limb_chain(J::R_UPPER_LEG, J::R_LOWER_LEG, J::R_FOOT, J::R_TOE,
                    glm::vec3(vis_rght.x, vis_rght.y, 0.0f), true);
                compute_limb_chain(J::L_UPPER_ARM, J::L_LOWER_ARM, J::L_HAND, J::L_HAND,
                    glm::vec3(-chest_fwd.x, -chest_fwd.y, 0.0f), false);
                compute_limb_chain(J::R_UPPER_ARM, J::R_LOWER_ARM, J::R_HAND, J::R_HAND,
                    glm::vec3(-chest_fwd.x, -chest_fwd.y, 0.0f), false);

                auto compute_clavicle = [&](Joint clav, Joint upper_arm,
                                            const glm::vec3 &ref) {
                    glm::vec3 bone_dir = jt[(int)upper_arm] - jt[(int)clav];
                    float blen = glm::length(bone_dir);
                    if (blen < 1e-5f) {
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)clav], 1.0f);
                        xforms.bones[(int)clav] = m;
                    } else {
                        glm::vec3 right, fwd, up;
                        build_bone_basis(bone_dir, ref, right, fwd, up);
                        xforms.bones[(int)clav] = make_bone_mat4(right, fwd, up, jt[(int)clav]);
                    }
                };
                compute_clavicle(J::L_CLAVICLE, J::L_UPPER_ARM, chest_fwd);
                compute_clavicle(J::R_CLAVICLE, J::R_UPPER_ARM, -chest_fwd);

                static constexpr Joint ik_joints[] = {
                    J::POLE_KNEE_L, J::POLE_KNEE_R,
                    J::POLE_ELBOW_L, J::POLE_ELBOW_R,
                    J::IK_FOOT_L, J::IK_FOOT_R,
                    J::IK_HAND_L, J::IK_HAND_R,
                };
                for (Joint j : ik_joints) {
                    glm::mat4 m(1.0f);
                    m[3] = glm::vec4(jt[(int)j], 1.0f);
                    xforms.bones[(int)j] = m;
                }

                (void)cfg;
                (void)chest_rght;
            });
        });

    ecs.system("MeshGenerationSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([](ActorTag,
                        const ActorConfig    &cfg,
                        const RigPose        &pose,
                        const RigTransforms  &xforms,
                        ProceduralMesh       &mesh) {
                using J = Joint;
                mesh.vertices.clear();
                mesh.vertices.reserve(2048);

                auto &v = mesh.vertices;
                const auto &b = xforms.bones;
                const auto &jt = pose.joints;

                glm::vec4 body(0.55f, 0.52f, 0.48f, 0.1f);

                append_bone_cage(v, b[(int)J::HIPS],     jt[(int)J::SPINE_01],
                                 cfg.torso_radius * 1.3f, cfg.torso_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::SPINE_01], jt[(int)J::SPINE_02],
                                 cfg.torso_radius * 1.0f, cfg.torso_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::SPINE_02], jt[(int)J::CHEST],
                                 cfg.torso_radius * 1.0f, cfg.torso_radius * 1.4f, 6, body);
                append_bone_cage(v, b[(int)J::CHEST],    jt[(int)J::NECK],
                                 cfg.torso_radius * 1.4f, cfg.neck_radius,         6, body);

                append_bone_cage(v, b[(int)J::NECK],     jt[(int)J::HEAD],
                                 cfg.head_radius * 0.4f,  cfg.head_radius * 0.6f, 6, body);
                append_bone_cage(v, b[(int)J::HEAD],     jt[(int)J::HEAD_END],
                                 cfg.head_radius * 0.6f,  cfg.head_radius * 0.3f, 6, body);

                append_bone_cage(v, b[(int)J::HIPS],          jt[(int)J::L_UPPER_LEG],
                                 cfg.leg_radius * 1.4f,       cfg.leg_radius * 1.4f, 6, body);
                append_bone_cage(v, b[(int)J::L_UPPER_LEG],   jt[(int)J::L_LOWER_LEG],
                                 cfg.leg_radius * 1.4f,       cfg.leg_radius * 0.9f, 6, body);
                append_bone_cage(v, b[(int)J::L_LOWER_LEG],   jt[(int)J::L_FOOT],
                                 cfg.leg_radius * 0.9f,       cfg.leg_radius * 0.8f, 6, body);
                append_bone_cage(v, b[(int)J::L_FOOT],        jt[(int)J::L_TOE],
                                 cfg.leg_radius * 0.6f,       cfg.leg_radius * 0.3f, 6, body);

                append_bone_cage(v, b[(int)J::HIPS],          jt[(int)J::R_UPPER_LEG],
                                 cfg.leg_radius * 1.4f,       cfg.leg_radius * 1.4f, 6, body);
                append_bone_cage(v, b[(int)J::R_UPPER_LEG],   jt[(int)J::R_LOWER_LEG],
                                 cfg.leg_radius * 1.4f,       cfg.leg_radius * 0.9f, 6, body);
                append_bone_cage(v, b[(int)J::R_LOWER_LEG],   jt[(int)J::R_FOOT],
                                 cfg.leg_radius * 0.9f,       cfg.leg_radius * 0.8f, 6, body);
                append_bone_cage(v, b[(int)J::R_FOOT],        jt[(int)J::R_TOE],
                                 cfg.leg_radius * 0.6f,       cfg.leg_radius * 0.3f, 6, body);

                append_bone_cage(v, b[(int)J::CHEST],         jt[(int)J::L_CLAVICLE],
                                 cfg.arm_radius * 1.5f,       cfg.arm_radius * 1.5f, 6, body);
                append_bone_cage(v, b[(int)J::L_CLAVICLE],    jt[(int)J::L_UPPER_ARM],
                                 cfg.arm_radius * 1.5f,       cfg.arm_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::L_UPPER_ARM],   jt[(int)J::L_LOWER_ARM],
                                 cfg.arm_radius * 1.0f,       cfg.arm_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::L_LOWER_ARM],   jt[(int)J::L_HAND],
                                 cfg.arm_radius * 1.0f,       cfg.arm_radius * 0.75f, 6, body);

                append_bone_cage(v, b[(int)J::CHEST],         jt[(int)J::R_CLAVICLE],
                                 cfg.arm_radius * 1.5f,       cfg.arm_radius * 1.5f, 6, body);
                append_bone_cage(v, b[(int)J::R_CLAVICLE],    jt[(int)J::R_UPPER_ARM],
                                 cfg.arm_radius * 1.5f,       cfg.arm_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::R_UPPER_ARM],   jt[(int)J::R_LOWER_ARM],
                                 cfg.arm_radius * 1.0f,       cfg.arm_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::R_LOWER_ARM],   jt[(int)J::R_HAND],
                                 cfg.arm_radius * 1.0f,       cfg.arm_radius * 0.75f, 6, body);
            });
        });

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
            anim_log.log_finalize(anim->support_balance,
                                   anim->lean_x, anim->lean_y);
            anim_log.log_camera(camera);
            anim_log.log_dynamics(*anim, *vel, dt);
            anim_log.log_arm_swing(*anim);
            anim_log.log_grounding(*anim, *legs);
            anim_log.end_frame();
        });
}
