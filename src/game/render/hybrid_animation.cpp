#include "render/hybrid_animation.h"
#include "render/anim_math.h"
#include "render/skinned_renderer.h"
#include "rig.h"
#include "input/input.h"
#include "terrain/map_util.h"
#include "game_state.h"
#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

static const AnimationConfig s_anim_cfg{};

static void player_movement(flecs::world &ecs, InputSystem &input,
                            flecs::entity player) {
    auto *phase = ecs.get<GamePhase>();
    if (!phase || phase->current != GamePhase::Playing) return;
    if (!player.is_alive()) return;

    auto *t    = player.get_mut<Transform>();
    auto *vel  = player.get_mut<Velocity>();
    auto *gait = player.get_mut<ProceduralGait>();
    auto *anim = player.get_mut<RigState>();
    if (!t || !vel || !gait || !anim) return;

    auto &in = input.state();
    float dt = ecs.delta_time();

    float raw_x = 0.0f, raw_y = 0.0f;
    if (in.held[(int)Action::MoveUp])    raw_y -= 1.0f;
    if (in.held[(int)Action::MoveDown])  raw_y += 1.0f;
    if (in.held[(int)Action::MoveLeft])  raw_x -= 1.0f;
    if (in.held[(int)Action::MoveRight]) raw_x += 1.0f;

    float raw_len = sqrtf(raw_x * raw_x + raw_y * raw_y);
    if (raw_len > 1e-6f) { raw_x /= raw_len; raw_y /= raw_len; }

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

    auto *look = player.get_mut<LookAtTarget>();
    if (look) {
        if (spd > 0.1f) {
            look->position = glm::vec3(t->x + vel->x / spd * 5.0f,
                                       t->y + vel->y / spd * 5.0f,
                                       t->z);
            look->active = true;
            look->weight = std::min(1.0f, look->weight + dt * 4.0f);
        } else {
            look->weight = std::max(0.0f, look->weight - dt * 2.0f);
            if (look->weight < 0.01f) look->active = false;
        }
    }
}

static void actor_grounding(flecs::world &ecs) {
    const auto *map_data = ecs.get<MapData>();
    if (!map_data || map_data->basalt_height.empty()) return;

    ecs.each([&](ActorTag, Transform &t, const ActorConfig &,
                 const LegState *) {
        t.z = sample_world_height(*map_data, t.x, t.y);
    });
}

static void clip_selection(SkinnedRenderer &skinned_renderer, flecs::entity player) {
    if (!player.is_alive()) return;
    const auto *vel = player.get<Velocity>();
    if (!vel) return;

    float speed = sqrtf(vel->x * vel->x + vel->y * vel->y);

    if (speed < 0.1f)       skinned_renderer.select_clip("idle", 0.35f);
    else if (speed < 5.0f)  skinned_renderer.select_clip("walk", 0.25f);
    else                    skinned_renderer.select_clip("run",  0.3f);
}

static void gait_sync(flecs::world &ecs, SkinnedRenderer &skinned_renderer,
                      flecs::entity player) {
    const auto *map_data = ecs.get<MapData>();
    if (!map_data || map_data->basalt_height.empty()) return;
    if (!player.is_alive()) return;

    float dt = ecs.delta_time();

    auto *t     = player.get_mut<Transform>();
    auto *vel   = player.get_mut<Velocity>();
    auto *gait  = player.get_mut<ProceduralGait>();
    auto *legs  = player.get_mut<LegState>();
    auto *anim  = player.get_mut<RigState>();
    auto *cfg   = player.get_mut<ActorConfig>();
    auto *pose  = player.get_mut<SkinnedPose>();
    if (!t || !vel || !gait || !legs || !anim || !cfg || !pose) return;

    skinned_renderer.update_mixer(dt);
    skinned_renderer.sample_pose(*pose);

    float speed = sqrtf(vel->x * vel->x + vel->y * vel->y);

    float ref_speed = gait->move_speed;
    if (speed > 0.1f && ref_speed > 0.01f) {
        float clip_speed = speed / ref_speed;
        clip_speed = std::clamp(clip_speed, 0.5f, 2.0f);
        skinned_renderer.set_playback_speed(clip_speed);
    } else {
        skinned_renderer.set_playback_speed(1.0f);
    }

    float fwd_x = cosf(t->facing), fwd_y = sinf(t->facing);
    float vel_dx = speed > 0.001f ? vel->x / speed : fwd_x;
    float vel_dy = speed > 0.001f ? vel->y / speed : fwd_y;

    float turn_urgency = anim->turn_urgency;

    float vf_fwd_x = cosf(anim->visual_facing);
    float vf_fwd_y = sinf(anim->visual_facing);

    float turn_blend = turn_urgency * 0.7f;
    float step_dx = vel_dx * (1.0f - turn_blend) + vf_fwd_x * turn_blend;
    float step_dy = vel_dy * (1.0f - turn_blend) + vf_fwd_y * turn_blend;
    float step_len = sqrtf(step_dx * step_dx + step_dy * step_dy);
    if (step_len > 0.001f) { step_dx /= step_len; step_dy /= step_len; }
    else { step_dx = vf_fwd_x; step_dy = vf_fwd_y; }

    constexpr float SWING_RATE = 2.7f;

    float dir_scale = 1.0f;
    if (speed > 0.01f) {
        float dx = vel->x / speed;
        float dy = vel->y / speed;
        float iso_align = fabsf(dx + dy) * 0.7071f;
        dir_scale = 1.0f + iso_align * s_anim_cfg.directional_speed_scale;
    }
    gait->phase += speed * dt * SWING_RATE * dir_scale;

    float step_rght_x = -step_dy, step_rght_y = step_dx;
    float hip_sign[2] = { -1.0f, 1.0f };

    StepTiming timing = gait_step_timing(speed, gait->move_speed,
                                         gait->step_duration, turn_urgency);
    float adaptive_duration = timing.adaptive_duration;
    float speed_factor      = timing.speed_factor;

    if (turn_urgency > 0.4f && !legs->stepping[0] && !legs->stepping[1]) {
        float behind[2];
        for (int i = 0; i < 2; ++i) {
            float fx = legs->foot[i].x - t->x;
            float fy = legs->foot[i].y - t->y;
            behind[i] = fx * step_dx + fy * step_dy;
        }
        legs->turn_step_queued = (behind[0] < behind[1]) ? 0 : 1;
    }
    if (turn_urgency <= 0.4f && legs->turn_step_queued >= 0
        && !legs->stepping[0] && !legs->stepping[1]) {
        legs->turn_step_queued = -1;
    }

    for (int leg = 0; leg < 2; ++leg) {
        int other_leg = 1 - leg;

        float hip_x = t->x + step_rght_x * hip_sign[leg] * cfg->hip_width;
        float hip_y = t->y + step_rght_y * hip_sign[leg] * cfg->hip_width;

        float half_stride = gait->stride_len * 0.5f;
        float center_off = half_stride * speed_factor;
        float center_x = hip_x + step_dx * center_off;
        float center_y = hip_y + step_dy * center_off;

        if (!legs->stepping[leg]) {
            float dx   = legs->foot[leg].x - center_x;
            float dy   = legs->foot[leg].y - center_y;
            float dist = sqrtf(dx * dx + dy * dy);

            bool other_planted = !legs->stepping[other_leg];
            float trigger_dist = gait_trigger_distance(half_stride, speed_factor);
            bool turn_blocked = (turn_urgency > 0.4f
                                 && legs->turn_step_queued >= 0
                                 && legs->turn_step_queued != leg);
            if (dist > trigger_dist && other_planted && !turn_blocked) {
                legs->stepping[leg]  = true;
                legs->progress[leg]  = 0.0f;
                legs->prev_foot[leg] = legs->foot[leg];
                if (legs->turn_step_queued == leg)
                    legs->turn_step_queued = -1;

                float step_travel = speed * adaptive_duration;
                float target_off  = (half_stride + step_travel * 0.75f) * speed_factor;
                float tgt_x = hip_x + step_dx * target_off;
                float tgt_y = hip_y + step_dy * target_off;

                float tgt_lat = (tgt_x - t->x) * step_rght_x + (tgt_y - t->y) * step_rght_y;
                if ((hip_sign[leg] < 0 && tgt_lat > 0.02f) || (hip_sign[leg] > 0 && tgt_lat < -0.02f)) {
                    tgt_x -= step_rght_x * (tgt_lat - hip_sign[leg] * 0.05f);
                    tgt_y -= step_rght_y * (tgt_lat - hip_sign[leg] * 0.05f);
                }

                float tgt_z = sphere_trace_height(*map_data, tgt_x, tgt_y, cfg->leg_radius);
                legs->target[leg]    = {tgt_x, tgt_y, tgt_z};
            }
        }

        if (legs->stepping[leg]) {
            legs->progress[leg] += dt / adaptive_duration;
            float progress = std::min(legs->progress[leg], 1.0f);

            legs->foot[leg] = gait_foot_arc(legs->prev_foot[leg], legs->target[leg],
                                            progress, gait->step_height);

            if (legs->progress[leg] >= 1.0f) {
                legs->stepping[leg] = false;
                legs->foot[leg]     = legs->target[leg];
                legs->plant_pos[leg] = legs->target[leg];
                legs->planted[leg]   = true;
            }
        }
    }

    for (int leg = 0; leg < 2; ++leg) {
        if (!legs->stepping[leg]) {
            float ground_z = sphere_trace_height(*map_data,
                                legs->foot[leg].x, legs->foot[leg].y, cfg->leg_radius);
            legs->foot[leg].z = ground_z;
        }
    }

    for (int leg = 0; leg < 2; ++leg) {
        if (legs->stepping[leg] || legs->stepping[1 - leg]) continue;
        float lat = (legs->foot[leg].x - t->x) * step_rght_x
                  + (legs->foot[leg].y - t->y) * step_rght_y;
        bool crossed = (hip_sign[leg] < 0) ? (lat > 0.02f) : (lat < -0.02f);
        if (crossed) {
            legs->stepping[leg] = true;
            legs->progress[leg] = 0.0f;
            legs->prev_foot[leg] = legs->foot[leg];
            float c_hip_x = t->x + step_rght_x * hip_sign[leg] * cfg->hip_width;
            float c_hip_y = t->y + step_rght_y * hip_sign[leg] * cfg->hip_width;
            float step_travel = speed * adaptive_duration;
            float target_off  = (gait->stride_len * 0.5f + step_travel * 0.75f) * speed_factor;
            float tgt_x = c_hip_x + step_dx * target_off;
            float tgt_y = c_hip_y + step_dy * target_off;
            legs->target[leg] = {tgt_x, tgt_y,
                sphere_trace_height(*map_data, tgt_x, tgt_y, cfg->leg_radius)};
        }
    }

    float max_horiz = gait->stride_len * 0.9f;
    for (int leg = 0; leg < 2; ++leg) {
        float hip_x = t->x + step_rght_x * hip_sign[leg] * cfg->hip_width;
        float hip_y = t->y + step_rght_y * hip_sign[leg] * cfg->hip_width;

        if (!legs->stepping[leg]) {
            if (legs->planted[leg]) {
                legs->foot[leg].x = legs->plant_pos[leg].x;
                legs->foot[leg].y = legs->plant_pos[leg].y;
            }
            float dx = legs->foot[leg].x - hip_x;
            float dy = legs->foot[leg].y - hip_y;
            float hd = sqrtf(dx * dx + dy * dy);
            if (hd > max_horiz) {
                if (!legs->stepping[1 - leg]) {
                    legs->stepping[leg]  = true;
                    legs->progress[leg]  = 0.0f;
                    legs->prev_foot[leg] = legs->foot[leg];
                    float step_travel = speed * adaptive_duration;
                    float target_off  = (gait->stride_len * 0.5f + step_travel * 0.75f) * speed_factor;
                    float tgt_x = hip_x + step_dx * target_off;
                    float tgt_y = hip_y + step_dy * target_off;
                    legs->target[leg] = {tgt_x, tgt_y,
                        sphere_trace_height(*map_data, tgt_x, tgt_y, cfg->leg_radius)};
                } else {
                    float s = max_horiz / hd;
                    legs->foot[leg].x = hip_x + dx * s;
                    legs->foot[leg].y = hip_y + dy * s;
                    legs->plant_pos[leg] = legs->foot[leg];
                }
            }
        } else if (legs->progress[leg] < 0.4f) {
            float tdx = legs->target[leg].x - hip_x;
            float tdy = legs->target[leg].y - hip_y;
            float th  = sqrtf(tdx * tdx + tdy * tdy);
            if (th > max_horiz) {
                float step_travel = speed * adaptive_duration;
                float target_off  = (gait->stride_len * 0.5f
                                    + step_travel * 0.75f) * speed_factor;
                legs->target[leg].x = hip_x + step_dx * target_off;
                legs->target[leg].y = hip_y + step_dy * target_off;
                legs->target[leg].z = sphere_trace_height(*map_data,
                                        legs->target[leg].x,
                                        legs->target[leg].y, cfg->leg_radius);
            }
        }
    }
}

static void additive_layer(flecs::world &ecs, SkinnedRenderer &skinned_renderer,
                           flecs::entity player) {
    if (!player.is_alive()) return;

    auto *pose = player.get_mut<SkinnedPose>();
    auto *t    = player.get<Transform>();
    auto *vel  = player.get<Velocity>();
    auto *cfg  = player.get<ActorConfig>();
    auto *gait = player.get<ProceduralGait>();
    auto *legs = player.get<LegState>();
    auto *anim = player.get_mut<RigState>();
    if (!pose || !t || !vel || !cfg || !gait || !anim) return;

    const BoneMap &bm = skinned_renderer.get_bone_map();
    int n = (int)pose->local_transforms.size();
    if (n == 0) return;
    float dt = ecs.delta_time();

    float speed   = sqrtf(vel->x * vel->x + vel->y * vel->y);

    float walk_blend = std::min(1.0f, speed / (gait->move_speed * 0.3f));
    float turn_urgency = std::min(1.0f, fabsf(anim->visual_facing_rate) / 10.0f);

    if (legs && bm.hips >= 0 && bm.hips < n) {
        float avg_foot_z = (legs->foot[0].z + legs->foot[1].z) * 0.5f;
        float terrain_delta = avg_foot_z - t->z;
        pose->local_transforms[bm.hips].translation.y += terrain_delta * 0.5f;
    }

    if (legs && bm.hips >= 0 && bm.hips < n) {
        float foot_diff = legs->foot[0].z - legs->foot[1].z;
        float max_tilt = glm::radians(8.0f);
        float hip_tilt_target = std::clamp(
            atan2f(foot_diff, cfg->hip_width * 2.0f),
            -max_tilt, max_tilt);
        anim->hip_tilt = smooth_damp(anim->hip_tilt, hip_tilt_target,
                                      &anim->hip_tilt_rate, 0.06f, dt);

        glm::quat tilt_rot = glm::angleAxis(anim->hip_tilt, glm::vec3(0.f, 0.f, 1.f));
        additive_rotation(pose->local_transforms[bm.hips], tilt_rot, 1.0f);
    }

    {
        float target_hip_roll = sinf(gait->phase) * 0.06f * walk_blend;
        anim->hip_roll = smooth_damp(anim->hip_roll, target_hip_roll,
                                      &anim->hip_roll_rate, 0.04f, dt);
        if (bm.hips >= 0 && bm.hips < n) {
            glm::quat roll_rot = glm::angleAxis(anim->hip_roll, glm::vec3(0.f, 0.f, 1.f));
            additive_rotation(pose->local_transforms[bm.hips], roll_rot, 1.0f);
        }
    }

    {
        float bob_blend = walk_blend * (1.0f - turn_urgency * 0.7f);
        float target_hip_bob = fabsf(sinf(gait->phase)) * 0.018f * bob_blend;
        anim->hip_bob = smooth_damp(anim->hip_bob, target_hip_bob,
                                     &anim->hip_bob_rate, 0.03f, dt);

        float step_dip_target = 0.0f;
        if (legs) {
            float leg_height_dip = cfg->leg_len + cfg->shin_len;
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
        anim->hip_dip = smooth_damp(anim->hip_dip, step_dip_target,
                                     &anim->hip_dip_rate, 0.025f, dt);

        float hip_z_offset = anim->hip_bob - anim->hip_dip;
        if (bm.hips >= 0 && bm.hips < n) {

            additive_translation(pose->local_transforms[bm.hips],
                                 glm::vec3(0.f, hip_z_offset, 0.f), 1.0f);
        }
    }

    {
        glm::vec3 cur_vel(vel->x, vel->y, 0.0f);
        glm::vec3 accel = (dt > 1e-6f) ? ((cur_vel - anim->prev_velocity) / dt)
                                       : glm::vec3(0.0f);
        anim->prev_velocity = cur_vel;

        float accel_len = glm::length(accel);
        const float max_lean = glm::radians(8.0f);
        float lean_factor = std::min(accel_len * 0.015f, max_lean);

        glm::vec3 lean_dir{0.0f};
        if (accel_len > 0.01f)
            lean_dir = glm::normalize(accel);

        float target_lean_x = lean_dir.x * lean_factor;
        float target_lean_y = lean_dir.y * lean_factor;

        float fwd_x = cosf(anim->visual_facing), fwd_y = sinf(anim->visual_facing);
        float speed_blend = std::min(1.0f, speed / gait->move_speed);
        float fwd_lean = speed_blend * glm::radians(2.5f);
        target_lean_x += fwd_x * fwd_lean;
        target_lean_y += fwd_y * fwd_lean;

        anim->chest_lean_x = smooth_damp(anim->chest_lean_x, target_lean_x,
                                           &anim->chest_lean_x_rate, 0.05f, dt);
        anim->chest_lean_y = smooth_damp(anim->chest_lean_y, target_lean_y,
                                           &anim->chest_lean_y_rate, 0.05f, dt);
        anim->neck_lean_x = smooth_damp(anim->neck_lean_x, anim->chest_lean_x * 0.7f,
                                         &anim->neck_lean_x_rate, 0.08f, dt);
        anim->neck_lean_y = smooth_damp(anim->neck_lean_y, anim->chest_lean_y * 0.7f,
                                         &anim->neck_lean_y_rate, 0.08f, dt);
        anim->head_lean_x = smooth_damp(anim->head_lean_x, anim->chest_lean_x * 0.5f,
                                         &anim->head_lean_x_rate, 0.12f, dt);
        anim->head_lean_y = smooth_damp(anim->head_lean_y, anim->chest_lean_y * 0.5f,
                                         &anim->head_lean_y_rate, 0.12f, dt);

        auto apply_lean_rot = [&](int bone_idx, float lx, float ly) {
            if (bone_idx < 0 || bone_idx >= n) return;

            float lean_mag = sqrtf(lx * lx + ly * ly);
            if (lean_mag < 1e-5f) return;

            float rel_fwd = lx * cosf(anim->visual_facing) + ly * sinf(anim->visual_facing);
            float rel_rght = -lx * sinf(anim->visual_facing) + ly * cosf(anim->visual_facing);

            glm::quat lean_q = glm::angleAxis(rel_fwd, glm::vec3(1.f, 0.f, 0.f))
                             * glm::angleAxis(-rel_rght, glm::vec3(0.f, 0.f, 1.f));
            additive_rotation(pose->local_transforms[bone_idx], lean_q, 1.0f);
        };

        apply_lean_rot(bm.chest, anim->chest_lean_x, anim->chest_lean_y);
        apply_lean_rot(bm.neck,  anim->neck_lean_x,  anim->neck_lean_y);
        apply_lean_rot(bm.head,  anim->head_lean_x,  anim->head_lean_y);
    }

    {
        float lag_angle = anim->visual_facing - anim->chest_facing;
        while (lag_angle >  glm::pi<float>()) lag_angle -= glm::two_pi<float>();
        while (lag_angle < -glm::pi<float>()) lag_angle += glm::two_pi<float>();

        if (bm.chest >= 0 && bm.chest < n) {
            float lag_rot = lag_angle * 0.25f;
            glm::quat chest_lag = glm::angleAxis(lag_rot, glm::vec3(0.f, 1.f, 0.f));
            additive_rotation(pose->local_transforms[bm.chest], chest_lag, 1.0f);
        }
    }

    {
        float idle_blend = 1.0f - std::min(1.0f, speed / 0.2f);

        anim->breath_phase += dt * glm::two_pi<float>() * 0.6f;
        float breath_offset = sinf(anim->breath_phase) * 0.008f * idle_blend;
        if (bm.chest >= 0 && bm.chest < n) {
            additive_translation(pose->local_transforms[bm.chest],
                                 glm::vec3(0.f, breath_offset, 0.f), 1.0f);
        }

        anim->idle_sway_phase += dt * glm::two_pi<float>() * 0.15f;
        float idle_sway = sinf(anim->idle_sway_phase) * 0.01f * idle_blend;
        if (bm.hips >= 0 && bm.hips < n) {

            glm::quat sway_q = glm::angleAxis(idle_sway, glm::vec3(0.f, 0.f, 1.f));
            additive_rotation(pose->local_transforms[bm.hips], sway_q, 1.0f);
        }

        if (idle_blend > 0.1f) {
            anim->idle_weight_phase += dt * glm::two_pi<float>() * 0.3f;
            float weight_shift = sinf(anim->idle_weight_phase);
            float hip_shift_angle = weight_shift * 0.04f * idle_blend;

            if (bm.hips >= 0 && bm.hips < n) {
                glm::quat shift_q = glm::angleAxis(hip_shift_angle, glm::vec3(0.f, 0.f, 1.f));
                additive_rotation(pose->local_transforms[bm.hips], shift_q, 1.0f);

                float weight_dip = (1.0f - fabsf(weight_shift)) * 0.012f * idle_blend;
                additive_translation(pose->local_transforms[bm.hips],
                                     glm::vec3(0.f, -weight_dip, 0.f), 1.0f);
            }
            if (bm.chest >= 0 && bm.chest < n) {
                glm::quat counter_q = glm::angleAxis(-hip_shift_angle * 0.3f, glm::vec3(0.f, 0.f, 1.f));
                additive_rotation(pose->local_transforms[bm.chest], counter_q, 1.0f);
            }
        }
    }
}

static void look_at(flecs::world &ecs, SkinnedRenderer &skinned_renderer,
                    flecs::entity player) {
    if (!player.is_alive()) return;

    float dt = ecs.delta_time();
    auto *pose = player.get_mut<SkinnedPose>();
    auto *t    = player.get<Transform>();
    auto *look = player.get<LookAtTarget>();
    auto *anim = player.get_mut<RigState>();
    if (!pose || !t || !look || !anim) return;

    const BoneMap &bm = skinned_renderer.get_bone_map();
    int n = (int)pose->local_transforms.size();

    if (!look->active && anim->look_yaw == 0.0f && anim->look_pitch == 0.0f)
        return;

    glm::vec3 head_pos(t->x, t->y, t->z + 1.5f);
    glm::vec3 to_target = look->position - head_pos;
    float horiz_dist = sqrtf(to_target.x * to_target.x + to_target.y * to_target.y);

    float target_yaw = 0.0f, target_pitch = 0.0f;
    if (horiz_dist > 0.01f) {
        float abs_yaw = atan2f(to_target.y, to_target.x);
        target_yaw = abs_yaw - t->facing;
        while (target_yaw >  glm::pi<float>()) target_yaw -= glm::two_pi<float>();
        while (target_yaw < -glm::pi<float>()) target_yaw += glm::two_pi<float>();
        target_pitch = atan2f(to_target.z, horiz_dist);
    }

    float max_yaw   = glm::radians(70.0f);
    float max_pitch = glm::radians(30.0f);
    target_yaw   = std::clamp(target_yaw,   -max_yaw,   max_yaw);
    target_pitch = std::clamp(target_pitch, -max_pitch, max_pitch);
    target_yaw   *= look->weight;
    target_pitch *= look->weight;

    anim->look_yaw = smooth_damp(anim->look_yaw, target_yaw,
                                   &anim->look_yaw_rate, 0.08f, dt);
    anim->look_pitch = smooth_damp(anim->look_pitch, target_pitch,
                                     &anim->look_pitch_rate, 0.08f, dt);

    struct LookEntry { int bone; float yaw_frac; float pitch_frac; };
    LookEntry entries[] = {
        { bm.head,  0.6f, 0.6f },
        { bm.neck,  0.3f, 0.3f },
        { bm.chest, 0.1f, 0.1f },
    };
    for (auto &e : entries) {
        if (e.bone < 0 || e.bone >= n) continue;
        float yaw   = anim->look_yaw   * e.yaw_frac;
        float pitch = anim->look_pitch  * e.pitch_frac;

        glm::quat look_q = glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f))
                         * glm::angleAxis(pitch, glm::vec3(1.f, 0.f, 0.f));
        additive_rotation(pose->local_transforms[e.bone], look_q, 1.0f);
    }
}

static void bone_palette(SkinnedRenderer &skinned_renderer, flecs::entity player) {
    if (!player.is_alive()) return;

    auto *pose = player.get<SkinnedPose>();
    auto *t    = player.get<Transform>();
    auto *anim = player.get<RigState>();
    if (!pose || !t || !anim || pose->local_transforms.empty()) return;

    auto locals = pose->local_transforms;
    const auto &skel = skinned_renderer.get_skeleton();
    if (!locals.empty() && !skel.bones.empty()) {
        locals[0].translation = glm::vec3(skel.bones[0].local_rest_transform[3]);
    }

    constexpr float kCharacterScale = 0.8f;
    constexpr float kIsoZScale = AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
    constexpr float kFacingOffset = glm::half_pi<float>();
    float s = kCharacterScale * skinned_renderer.debug_uniform_scale;
    glm::vec3 scale_vec(s, s * kIsoZScale, s);
    glm::vec3 player_pos(t->x, t->y, t->z);

    glm::mat4 root = glm::translate(glm::mat4(1.f), player_pos)
                   * glm::rotate(glm::mat4(1.f), anim->visual_facing + kFacingOffset, glm::vec3(0.f, 0.f, 1.f))
                   * glm::rotate(glm::mat4(1.f), glm::radians(90.0f), glm::vec3(1.f, 0.f, 0.f))
                   * glm::scale(glm::mat4(1.f), scale_vec);

    BonePalette palette = compute_bone_palette(skel, locals, root);
    skinned_renderer.set_bone_palette(palette);
}

void register_hybrid_systems(flecs::world &ecs,
                              InputSystem    &input,
                              SkinnedRenderer &skinned_renderer,
                              flecs::entity   player_entity) {
    auto post = [&ecs](const char *name) {
        return ecs.system(name).kind(flecs::PostUpdate);
    };

    post("PlayerMovementSystem").run([&input, &ecs, player_entity](flecs::iter &) {
        player_movement(ecs, input, player_entity);
    });
    post("ActorGroundingSystem").run([&ecs](flecs::iter &) {
        actor_grounding(ecs);
    });
    post("ClipSelectionSystem").run([&skinned_renderer, player_entity](flecs::iter &) {
        clip_selection(skinned_renderer, player_entity);
    });
    post("GaitSyncSystem").run([&ecs, &skinned_renderer, player_entity](flecs::iter &) {
        gait_sync(ecs, skinned_renderer, player_entity);
    });
    post("AdditiveLayerSystem").run([&ecs, &skinned_renderer, player_entity](flecs::iter &) {
        additive_layer(ecs, skinned_renderer, player_entity);
    });
    post("LookAtSystem").run([&ecs, &skinned_renderer, player_entity](flecs::iter &) {
        look_at(ecs, skinned_renderer, player_entity);
    });
    post("BonePaletteSystem").run([&skinned_renderer, player_entity](flecs::iter &) {
        bone_palette(skinned_renderer, player_entity);
    });
}
