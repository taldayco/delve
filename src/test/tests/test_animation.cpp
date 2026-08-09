#include "rig.h"
#include "render/anim_math.h"
#include "render/skeletal_animation.h"
#include "config.h"
#include "test_harness.h"
#include "terrain/map_util.h"
#include "terrain/map_data.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// ---- Component defaults --------------------------------------------------

DELVE_TEST(default_gait_parameters_valid) {
  ProceduralGait g;
  EXPECT_GT(g.stride_len, 0.0f);
  EXPECT_GT(g.step_height, 0.0f);
  EXPECT_GT(g.step_duration, 0.0f);
  EXPECT_GT(g.move_speed, 0.0f);
  return true;
}

DELVE_TEST(default_actor_config_proportional) {
  ActorConfig c;
  EXPECT_GT(c.leg_len, c.arm_len);
  EXPECT_GT(c.torso_len, 0.3f);
  EXPECT_LT(c.head_radius, c.torso_len);
  EXPECT_GT(c.shoulder_width, c.hip_width);
  return true;
}

DELVE_TEST(leg_state_defaults_not_stepping) {
  LegState legs;
  EXPECT_FALSE(legs.stepping[0]);
  EXPECT_FALSE(legs.stepping[1]);
  EXPECT_EQ(legs.turn_step_queued, -1);
  return true;
}

// ---- smooth_damp (the shipped implementation) ----------------------------

DELVE_TEST(smooth_damp_convergence) {
  float current = 0.0f, target = 1.0f, rate = 0.0f;
  float dt = 1.0f / 60.0f;
  for (int i = 0; i < 60; ++i)
    current = smooth_damp(current, target, &rate, 0.1f, dt);
  EXPECT_GT(current, 0.99f);
  return true;
}

DELVE_TEST(smooth_damp_no_overshoot_from_rest) {
  float current = 0.0f, target = 1.0f, rate = 0.0f;
  float dt = 1.0f / 60.0f;
  for (int i = 0; i < 300; ++i) {
    current = smooth_damp(current, target, &rate, 0.1f, dt);
    EXPECT_LT(current, 1.0001f);
  }
  return true;
}

DELVE_TEST(smooth_damp_angle_convergence) {
  float current = 0.0f;
  float target = glm::half_pi<float>();
  float velocity = 0.0f;
  float dt = 1.0f / 60.0f;

  for (int i = 0; i < 300; ++i)
    current = smooth_damp_angle(current, target, &velocity, 0.15f, dt);

  EXPECT_NEAR(current, target, 0.01f);
  return true;
}

DELVE_TEST(smooth_damp_angle_wraps_around) {
  // -170° to +170° should go the short way (through 180°), i.e. decrease.
  float current  = glm::radians(-170.0f);
  float target   = glm::radians(170.0f);
  float velocity = 0.0f;
  float dt = 1.0f / 60.0f;

  float after_one = smooth_damp_angle(current, target, &velocity, 0.15f, dt);
  EXPECT_LT(after_one, current);
  return true;
}

// ---- Gait math (shared with GaitSyncSystem) ------------------------------

DELVE_TEST(gait_foot_arc_endpoints_and_peak) {
  glm::vec3 prev{0, 0, 0}, target{1, 0, 0};
  float sh = 0.5f;
  glm::vec3 at0  = gait_foot_arc(prev, target, 0.0f, sh);
  glm::vec3 at05 = gait_foot_arc(prev, target, 0.5f, sh);
  glm::vec3 at1  = gait_foot_arc(prev, target, 1.0f, sh);

  EXPECT_NEAR(at0.z,  0.0f, 1e-4f);
  EXPECT_NEAR(at1.z,  0.0f, 1e-4f);
  EXPECT_NEAR(at05.z, sh,   0.02f);
  EXPECT_GT(at05.z, at0.z);
  EXPECT_GT(at05.z, at1.z);
  return true;
}

DELVE_TEST(gait_foot_arc_velocity_peaks_at_midstride) {
  glm::vec3 prev{0, 0, 0}, target{2, 0, 0};
  float dp = 0.05f;
  float v_start = gait_foot_arc(prev, target, dp, 0.0f).x
                - gait_foot_arc(prev, target, 0.0f, 0.0f).x;
  float v_mid   = gait_foot_arc(prev, target, 0.5f + dp, 0.0f).x
                - gait_foot_arc(prev, target, 0.5f, 0.0f).x;
  float v_end   = gait_foot_arc(prev, target, 1.0f, 0.0f).x
                - gait_foot_arc(prev, target, 1.0f - dp, 0.0f).x;

  EXPECT_GT(v_mid, v_start);
  EXPECT_GT(v_mid, v_end);
  EXPECT_LT(v_start, v_mid * 0.5f);
  return true;
}

DELVE_TEST(gait_foot_arc_reaches_target_at_t1) {
  glm::vec3 prev{3, -1, 2}, target{5, 2, 1};
  glm::vec3 at1 = gait_foot_arc(prev, target, 1.0f, 0.0f);
  EXPECT_NEAR(at1.x, target.x, 1e-4f);
  EXPECT_NEAR(at1.y, target.y, 1e-4f);
  EXPECT_NEAR(at1.z, target.z, 1e-4f);
  return true;
}

DELVE_TEST(gait_step_timing_faster_at_speed) {
  ProceduralGait g;
  StepTiming slow = gait_step_timing(g.move_speed * 0.4f, g.move_speed,
                                     g.step_duration, 0.0f);
  StepTiming fast = gait_step_timing(g.move_speed, g.move_speed,
                                     g.step_duration, 0.0f);
  EXPECT_LT(fast.adaptive_duration, slow.adaptive_duration);
  EXPECT_GE(fast.speed_factor, slow.speed_factor);
  return true;
}

DELVE_TEST(gait_step_timing_turning_quickens_steps) {
  ProceduralGait g;
  StepTiming straight = gait_step_timing(g.move_speed, g.move_speed,
                                         g.step_duration, 0.0f);
  StepTiming turning  = gait_step_timing(g.move_speed, g.move_speed,
                                         g.step_duration, 1.0f);
  EXPECT_LT(turning.adaptive_duration, straight.adaptive_duration);
  EXPECT_LT(turning.speed_factor, straight.speed_factor);
  return true;
}

DELVE_TEST(gait_trigger_distance_scales_with_speed_factor) {
  float half_stride = 0.5f;
  float at_rest = gait_trigger_distance(half_stride, 0.0f);
  float at_full = gait_trigger_distance(half_stride, 1.0f);
  EXPECT_GT(at_rest, 0.0f);
  EXPECT_NEAR(at_full, half_stride, 1e-5f);
  EXPECT_LT(at_rest, at_full);
  return true;
}

// ---- Terrain sampling (map_util) -----------------------------------------

static MapData make_slope_map(int w, int h, float slope) {
  MapData map;
  map.width = w;
  map.height = h;
  map.basalt_height.resize(w * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      map.basalt_height[y * w + x] = slope * (float)x;
  return map;
}

static MapData make_flat_map(int w, int h, float height) {
  MapData map;
  map.width = w;
  map.height = h;
  map.basalt_height.resize(w * h, height);
  return map;
}

DELVE_TEST(sphere_trace_ge_point_sample) {
  auto map = make_slope_map(64, 64, 0.5f);
  float radii[] = {0.05f, 0.1f, 0.2f};
  float positions[] = {1.0f, 2.0f, 3.0f, 4.0f};
  for (float wx : positions) {
    float wy = 2.0f;
    float point_h = sample_world_height(map, wx, wy);
    for (float r : radii) {
      float sphere_h = sphere_trace_height(map, wx, wy, r);
      EXPECT_GE(sphere_h, point_h);
    }
  }
  return true;
}

DELVE_TEST(sphere_trace_flat_equals_point) {
  auto map = make_flat_map(32, 32, 5.0f);
  float wx = 1.5f, wy = 1.5f;
  float point_h = sample_world_height(map, wx, wy);
  float sphere_h = sphere_trace_height(map, wx, wy, 0.1f);
  EXPECT_NEAR(sphere_h, point_h, 1e-5f);
  return true;
}

DELVE_TEST(sphere_trace_radius_zero_equals_point) {
  auto map = make_slope_map(64, 64, 0.3f);
  float wx = 2.0f, wy = 2.0f;
  float point_h = sample_world_height(map, wx, wy);
  float sphere_h = sphere_trace_height(map, wx, wy, 0.0f);
  EXPECT_NEAR(sphere_h, point_h, 1e-6f);
  return true;
}

DELVE_TEST(foot_z_clears_terrain_on_slope) {
  auto map = make_slope_map(64, 64, 0.4f);
  ActorConfig cfg;
  float wx = 3.0f, wy = 3.0f;
  float foot_z = sphere_trace_height(map, wx, wy, cfg.leg_radius);

  for (int i = 0; i < 8; ++i) {
    float angle = i * (2.0f * glm::pi<float>() / 8.0f);
    float sx = wx + cfg.leg_radius * cosf(angle);
    float sy = wy + cfg.leg_radius * sinf(angle);
    float terrain_h = sample_world_height(map, sx, sy);
    EXPECT_GE(foot_z, terrain_h);
  }
  return true;
}

// ---- additive_rotation (used by AdditiveLayerSystem) ---------------------

DELVE_TEST(additive_rotation_preserves_axis_on_identity_base) {
  BoneLocalTransform xf;
  glm::quat roll = glm::angleAxis(0.3f, glm::vec3(0.f, 0.f, 1.f));
  additive_rotation(xf, roll, 1.0f);
  glm::vec3 axis = glm::axis(xf.rotation);
  EXPECT_NEAR(fabsf(axis.z), 1.0f, 1e-4f);
  EXPECT_NEAR(glm::angle(xf.rotation), 0.3f, 1e-4f);
  return true;
}

DELVE_TEST(additive_rotation_zero_weight_is_identity) {
  BoneLocalTransform xf;
  glm::quat base = xf.rotation;
  glm::quat roll = glm::angleAxis(0.7f, glm::vec3(1.f, 0.f, 0.f));
  additive_rotation(xf, roll, 0.0f);
  EXPECT_NEAR(glm::dot(xf.rotation, base), 1.0f, 1e-5f);
  return true;
}

// ---- AnimationMixer keyframe sampling ------------------------------------

static GltfAnimationClip make_test_clip() {
  GltfAnimationClip clip;
  clip.duration = 1.0f;
  GltfAnimChannel ch;
  ch.bone_index = 0;
  ch.path = "translation";
  ch.times = {0.0f, 1.0f};
  ch.translations = {glm::vec3(0.f), glm::vec3(2.f, 0.f, 0.f)};
  clip.channels.push_back(ch);
  return clip;
}

DELVE_TEST(mixer_sample_clip_interpolates_keyframes) {
  auto clip = make_test_clip();
  std::vector<BoneLocalTransform> out(1);

  AnimationMixer::sample_clip(&clip, 0.5f, out);
  EXPECT_NEAR(out[0].translation.x, 1.0f, 1e-4f);

  AnimationMixer::sample_clip(&clip, 0.0f, out);
  EXPECT_NEAR(out[0].translation.x, 0.0f, 1e-4f);

  AnimationMixer::sample_clip(&clip, 1.0f, out);
  EXPECT_NEAR(out[0].translation.x, 2.0f, 1e-4f);
  return true;
}

DELVE_TEST(mixer_sample_clip_clamps_out_of_range_time) {
  auto clip = make_test_clip();
  std::vector<BoneLocalTransform> out(1);

  AnimationMixer::sample_clip(&clip, -1.0f, out);
  EXPECT_NEAR(out[0].translation.x, 0.0f, 1e-4f);

  AnimationMixer::sample_clip(&clip, 5.0f, out);
  EXPECT_NEAR(out[0].translation.x, 2.0f, 1e-4f);
  return true;
}

DELVE_TEST(mixer_untracked_bone_keeps_existing_value) {
  auto clip = make_test_clip();
  std::vector<BoneLocalTransform> out(2);
  out[1].translation = glm::vec3(7.f, 8.f, 9.f);

  AnimationMixer::sample_clip(&clip, 0.5f, out);
  EXPECT_NEAR(out[1].translation.x, 7.0f, 1e-5f);
  return true;
}

// ---- BoneMap name matching -----------------------------------------------

DELVE_TEST(bone_map_matches_mixamo_style_names) {
  GltfSkeleton skel;
  const char *names[] = {"Hips", "Spine", "Chest", "Neck", "Head",
                         "LeftUpLeg", "LeftLeg", "LeftFoot",
                         "RightUpLeg", "RightLeg", "RightFoot"};
  for (const char *name : names) {
    GltfBone bone;
    bone.name = name;
    skel.bones.push_back(bone);
  }

  BoneMap m = BoneMap::build_from_skeleton(skel);
  EXPECT_EQ(m.hips, 0);
  EXPECT_EQ(m.spine, 1);
  EXPECT_EQ(m.chest, 2);
  EXPECT_EQ(m.neck, 3);
  EXPECT_EQ(m.head, 4);
  EXPECT_EQ(m.l_upper_leg, 5);
  EXPECT_EQ(m.l_lower_leg, 6);
  EXPECT_EQ(m.l_foot, 7);
  EXPECT_EQ(m.r_upper_leg, 8);
  EXPECT_EQ(m.r_lower_leg, 9);
  EXPECT_EQ(m.r_foot, 10);
  return true;
}
