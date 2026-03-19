#include "render/hybrid_animation.h"
#include "render/anim_math.h"
#include "render/skinned_renderer.h"
#include "rig.h"
#include "animation_log.h"
#include "input/input.h"
#include "camera/camera.h"
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

void register_hybrid_systems(flecs::world &ecs,
                              InputSystem    &input,
                              CameraState    &camera,
                              AnimationLogger &anim_log,
                              SkinnedRenderer &skinned_renderer,
                              flecs::entity   player_entity) {

    // ---- 1. PlayerMovementSystem ----
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

    // ---- 2. ActorGroundingSystem ----
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

    // ---- 3. ClipSelectionSystem ----
    ecs.system("ClipSelectionSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, &skinned_renderer, player_entity](flecs::iter &) {
            if (!player_entity.is_alive()) return;
            const auto *vel = player_entity.get<Velocity>();
            if (!vel) return;

            float speed = sqrtf(vel->x * vel->x + vel->y * vel->y);

            if (speed < 0.1f)       skinned_renderer.select_clip("idle", 0.35f);
            else if (speed < 5.0f)  skinned_renderer.select_clip("walk", 0.25f);
            else                    skinned_renderer.select_clip("run",  0.3f);
        });

    // ---- 4. GaitSyncSystem ----
    ecs.system("GaitSyncSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, &skinned_renderer, player_entity](flecs::iter &) {
            const auto *map_data = ecs.get<MapData>();
            if (!map_data || map_data->basalt_height.empty()) return;
            if (!player_entity.is_alive()) return;

            float dt = ecs.delta_time();

            auto *t     = player_entity.get_mut<Transform>();
            auto *vel   = player_entity.get_mut<Velocity>();
            auto *gait  = player_entity.get_mut<ProceduralGait>();
            auto *legs  = player_entity.get_mut<LegState>();
            auto *anim  = player_entity.get_mut<RigState>();
            auto *cfg   = player_entity.get_mut<ActorConfig>();
            auto *pose  = player_entity.get_mut<SkinnedPose>();
            if (!t || !vel || !gait || !legs || !anim || !cfg || !pose) return;

            // Update mixer and sample blended pose
            skinned_renderer.update_mixer(dt);
            skinned_renderer.sample_pose(*pose);

            float speed = sqrtf(vel->x * vel->x + vel->y * vel->y);

            // Adjust playback speed to match movement
            float ref_speed = gait->move_speed;
            if (speed > 0.1f && ref_speed > 0.01f) {
                float clip_speed = speed / ref_speed;
                clip_speed = std::clamp(clip_speed, 0.5f, 2.0f);
                skinned_renderer.set_playback_speed(clip_speed);
            } else {
                skinned_renderer.set_playback_speed(1.0f);
            }

            // --- Foot placement (ported from GaitSystem) ---
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

            float speed_ratio      = std::max(0.4f, std::min(1.0f, speed / gait->move_speed));
            float adaptive_duration = gait->step_duration / speed_ratio;
            adaptive_duration *= (1.0f - turn_urgency * 0.3f);

            float speed_factor = std::min(1.0f, speed / (gait->move_speed * 0.15f));
            speed_factor *= (1.0f - turn_urgency * 0.5f);

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
                    float trigger_dist = half_stride * (0.25f + 0.75f * speed_factor);
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

                    float warped_t = progress * progress * progress
                                     * (progress * (progress * 6.0f - 15.0f) + 10.0f);
                    float ts_z  = progress * progress * (3.0f - 2.0f * progress);

                    legs->foot[leg].x = legs->prev_foot[leg].x + (legs->target[leg].x - legs->prev_foot[leg].x) * warped_t;
                    legs->foot[leg].y = legs->prev_foot[leg].y + (legs->target[leg].y - legs->prev_foot[leg].y) * warped_t;
                    legs->foot[leg].z = legs->prev_foot[leg].z + (legs->target[leg].z - legs->prev_foot[leg].z) * ts_z
                                       + sinf(progress * glm::pi<float>()) * gait->step_height;

                    if (legs->progress[leg] >= 1.0f) {
                        float step_dist = glm::length(legs->target[leg] - legs->prev_foot[leg]);
                        anim->foot_contact_velocity[leg] = step_dist / adaptive_duration;

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
        });

    // ---- 5. GrabDriveSystem ----
    ecs.system("GrabDriveSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([&](ActorTag,
                         const GrabState       &grab,
                         const ActorConfig     &cfg,
                         ArmIKGoal             &arm_ik,
                         ProceduralGait        &gait,
                         AnimationOverlay      *overlay) {

                (void)cfg; (void)gait;
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

    // ---- 6. HybridIKSystem ----
    // Foot IK: compute world-space foot from SkinnedPose via FK,
    // then override bone local rotations to match procedural foot targets.
    // For now, foot placement data is stored in LegState and used by
    // AdditiveLayerSystem to adjust hip height. Full bone-local IK TBD.
    ecs.system("HybridIKSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, &skinned_renderer, player_entity](flecs::iter &) {
            if (!player_entity.is_alive()) return;
            auto *pose = player_entity.get_mut<SkinnedPose>();
            auto *legs = player_entity.get<LegState>();
            auto *cfg  = player_entity.get<ActorConfig>();
            auto *t    = player_entity.get<Transform>();
            auto *anim = player_entity.get<RigState>();
            auto *arm_ik = player_entity.get<ArmIKGoal>();
            if (!pose || !legs || !cfg || !t || !anim) return;

            const BoneMap &bm = skinned_renderer.get_bone_map();
            int n = (int)pose->local_transforms.size();
            if (n == 0) return;

            // Foot IK: adjust hip height based on foot placement
            // The procedural feet are tracked in world space (LegState).
            // We adjust the hips bone translation to lower/raise the character
            // so feet meet the terrain.
            if (bm.hips >= 0 && bm.hips < n) {
                float avg_foot_z = (legs->foot[0].z + legs->foot[1].z) * 0.5f;
                float terrain_delta = avg_foot_z - t->z;
                // Apply hip offset in local space (Y-up in glTF, mapped to Z-up in game)
                // The 90-degree X rotation maps glTF Y to game Z
                pose->local_transforms[bm.hips].translation.y += terrain_delta * 0.5f;
            }

            // Arm IK via solve_two_bone when grab is active
            // (Full bone-local conversion would require FK chain computation;
            //  for now we note the targets for future refinement)
            (void)arm_ik;
        });

    // ---- 7. AdditiveLayerSystem ----
    ecs.system("AdditiveLayerSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, &skinned_renderer, player_entity](flecs::iter &) {
            if (!player_entity.is_alive()) return;

            auto *pose = player_entity.get_mut<SkinnedPose>();
            auto *t    = player_entity.get<Transform>();
            auto *vel  = player_entity.get<Velocity>();
            auto *cfg  = player_entity.get<ActorConfig>();
            auto *gait = player_entity.get<ProceduralGait>();
            auto *legs = player_entity.get<LegState>();
            auto *anim = player_entity.get_mut<RigState>();
            auto *overlay = player_entity.get_mut<AnimationOverlay>();
            if (!pose || !t || !vel || !cfg || !gait || !anim) return;

            const BoneMap &bm = skinned_renderer.get_bone_map();
            int n = (int)pose->local_transforms.size();
            if (n == 0) return;
            float dt = ecs.delta_time();

            float speed   = sqrtf(vel->x * vel->x + vel->y * vel->y);
            float rght_x  = -sinf(anim->visual_facing), rght_y = cosf(anim->visual_facing);

            float walk_blend = std::min(1.0f, speed / (gait->move_speed * 0.3f));
            float turn_urgency = std::min(1.0f, fabsf(anim->visual_facing_rate) / 10.0f);

            // --- Hip dynamics (as additive rotations/translations on hips bone) ---

            // Hip tilt from foot height difference
            if (legs && bm.hips >= 0 && bm.hips < n) {
                float foot_diff = legs->foot[0].z - legs->foot[1].z;
                float max_tilt = glm::radians(8.0f);
                float hip_tilt_target = std::clamp(
                    atan2f(foot_diff, cfg->hip_width * 2.0f),
                    -max_tilt, max_tilt);
                anim->hip_tilt = smooth_damp(anim->hip_tilt, hip_tilt_target,
                                              &anim->hip_tilt_rate, 0.06f, dt);
                // Additive X-rotation on hips (roll around forward axis)
                glm::quat tilt_rot = glm::angleAxis(anim->hip_tilt, glm::vec3(1.f, 0.f, 0.f));
                additive_rotation(pose->local_transforms[bm.hips], tilt_rot, 1.0f);
            }

            // Hip roll (Z-rotation in game space → Z-rotation in bone local Y-up)
            {
                float target_hip_roll = sinf(gait->phase) * 0.06f * walk_blend;
                anim->hip_roll = smooth_damp(anim->hip_roll, target_hip_roll,
                                              &anim->hip_roll_rate, 0.04f, dt);
                if (bm.hips >= 0 && bm.hips < n) {
                    glm::quat roll_rot = glm::angleAxis(anim->hip_roll, glm::vec3(0.f, 0.f, 1.f));
                    additive_rotation(pose->local_transforms[bm.hips], roll_rot, 1.0f);
                }
            }

            // Hip bob (additive Y-translation in bone space)
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
                    // In glTF Y-up space, game Z maps to bone Y
                    additive_translation(pose->local_transforms[bm.hips],
                                         glm::vec3(0.f, hip_z_offset, 0.f), 1.0f);
                }
            }

            // --- Acceleration lean (cascading chest/neck/head) ---
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

                anim->lean_x = anim->chest_lean_x;
                anim->lean_y = anim->chest_lean_y;

                // Apply lean as additive rotations
                auto apply_lean_rot = [&](int bone_idx, float lx, float ly) {
                    if (bone_idx < 0 || bone_idx >= n) return;
                    // Convert world-space lean to local pitch/roll
                    float lean_mag = sqrtf(lx * lx + ly * ly);
                    if (lean_mag < 1e-5f) return;
                    // Lean direction relative to facing
                    float rel_fwd = lx * cosf(anim->visual_facing) + ly * sinf(anim->visual_facing);
                    float rel_rght = -lx * sinf(anim->visual_facing) + ly * cosf(anim->visual_facing);
                    // In glTF Y-up: X-rot = pitch forward, Z-rot = roll lateral
                    glm::quat lean_q = glm::angleAxis(rel_fwd, glm::vec3(1.f, 0.f, 0.f))
                                     * glm::angleAxis(-rel_rght, glm::vec3(0.f, 0.f, 1.f));
                    additive_rotation(pose->local_transforms[bone_idx], lean_q, 1.0f);
                };

                apply_lean_rot(bm.chest, anim->chest_lean_x, anim->chest_lean_y);
                apply_lean_rot(bm.neck,  anim->neck_lean_x,  anim->neck_lean_y);
                apply_lean_rot(bm.head,  anim->head_lean_x,  anim->head_lean_y);
            }

            // --- Chest facing lag ---
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

            // --- Breathing + idle sway ---
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
                    // Sway as lateral rotation on hips
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

            // --- Overlays ---
            if (overlay) {
                using OT = AnimationOverlay::Type;
                if (overlay->active != OT::None && overlay->intensity > 0.001f) {
                    overlay->phase += dt * 2.0f;
                    float I = overlay->intensity;

                    switch (overlay->active) {
                    case OT::Limp:
                        if (bm.hips >= 0 && bm.hips < n) {
                            float limp_drop = sinf(gait->phase) * 0.04f * I;
                            additive_translation(pose->local_transforms[bm.hips],
                                                 glm::vec3(0.f, -fabsf(limp_drop) * 0.3f, 0.f), 1.0f);
                        }
                        break;
                    case OT::Fatigue:
                        if (bm.chest >= 0 && bm.chest < n) {
                            float fatigue_sway = sinf(overlay->phase * 0.8f) * 0.025f * I;
                            glm::quat fatigue_q = glm::angleAxis(fatigue_sway, glm::vec3(0.f, 0.f, 1.f));
                            additive_rotation(pose->local_transforms[bm.chest], fatigue_q, 1.0f);
                            float lean = 0.03f * I;
                            glm::quat lean_q = glm::angleAxis(lean, glm::vec3(1.f, 0.f, 0.f));
                            additive_rotation(pose->local_transforms[bm.chest], lean_q, 1.0f);
                        }
                        break;
                    case OT::HeavyCarry:
                        if (bm.chest >= 0 && bm.chest < n) {
                            float lean = 0.05f * I;
                            glm::quat carry_q = glm::angleAxis(lean, glm::vec3(1.f, 0.f, 0.f));
                            additive_rotation(pose->local_transforms[bm.chest], carry_q, 1.0f);
                        }
                        break;
                    case OT::None: break;
                    }
                }
            }
        });

    // ---- 8. LookAtSystem ----
    ecs.system("LookAtSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, &skinned_renderer, player_entity](flecs::iter &) {
            if (!player_entity.is_alive()) return;

            float dt = ecs.delta_time();
            auto *pose = player_entity.get_mut<SkinnedPose>();
            auto *t    = player_entity.get<Transform>();
            auto *look = player_entity.get<LookAtTarget>();
            auto *anim = player_entity.get_mut<RigState>();
            if (!pose || !t || !look || !anim) return;

            const BoneMap &bm = skinned_renderer.get_bone_map();
            int n = (int)pose->local_transforms.size();

            if (!look->active && anim->look_yaw == 0.0f && anim->look_pitch == 0.0f)
                return;

            // Compute target yaw/pitch relative to facing
            glm::vec3 head_pos(t->x, t->y, t->z + 1.5f); // approximate head world pos
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

            // Distribute: head 60%, neck 30%, chest 10%
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
                // In glTF Y-up: yaw around Y, pitch around X
                glm::quat look_q = glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f))
                                 * glm::angleAxis(pitch, glm::vec3(1.f, 0.f, 0.f));
                additive_rotation(pose->local_transforms[e.bone], look_q, 1.0f);
            }
        });

    // ---- 9. BonePaletteSystem ----
    ecs.system("BonePaletteSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs, &skinned_renderer, player_entity](flecs::iter &) {
            if (!player_entity.is_alive()) return;

            auto *pose = player_entity.get<SkinnedPose>();
            auto *t    = player_entity.get<Transform>();
            auto *anim = player_entity.get<RigState>();
            if (!pose || !t || !anim || pose->local_transforms.empty()) return;

            // Strip root motion
            auto locals = pose->local_transforms;
            const auto &skel = skinned_renderer.get_skeleton();
            if (!locals.empty() && !skel.bones.empty()) {
                locals[0].translation = glm::vec3(skel.bones[0].local_rest_transform[3]);
            }

            // Build root world transform
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
        });

    // ---- Animation Log System ----
    ecs.system("AnimationLogSystem")
        .kind(flecs::PostUpdate)
        .run([&anim_log, &ecs, &camera, player_entity](flecs::iter &) {
            if (!anim_log.active) return;
            if (!player_entity.is_alive()) return;

            const auto *t    = player_entity.get<Transform>();
            const auto *vel  = player_entity.get<Velocity>();
            const auto *gait = player_entity.get<ProceduralGait>();
            const auto *legs = player_entity.get<LegState>();
            const auto *cfg  = player_entity.get<ActorConfig>();
            const auto *anim = player_entity.get<RigState>();
            if (!t || !vel || !gait || !legs || !cfg || !anim) return;

            float dt = ecs.delta_time();
            anim_log.begin_frame(dt);
            anim_log.log_transform(*t, *vel);
            anim_log.log_gait(*gait);
            anim_log.log_legs(*legs, *t, *cfg);
            anim_log.log_finalize(anim->support_balance,
                                   anim->lean_x, anim->lean_y);
            anim_log.log_camera(camera);
            anim_log.log_dynamics(*anim, *vel, dt);
            anim_log.log_arm_swing(*anim);
            anim_log.log_grounding(*anim, *legs);
            anim_log.end_frame();
        });
}
