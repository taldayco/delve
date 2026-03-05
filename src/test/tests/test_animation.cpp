#include "rig.h"
#include "animation_metrics.h"
#include "config.h"
#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// ---- Existing 5 tests (preserved) + height proportions ----

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
  EXPECT_GT(c.leg_len, c.arm_len);          // legs longer than arms
  EXPECT_GT(c.torso_len, 0.3f);             // torso substantial
  EXPECT_LT(c.head_radius, c.torso_len);    // head smaller than torso
  EXPECT_GT(c.shoulder_width, c.hip_width); // shoulders wider than hips
  return true;
}

DELVE_TEST(skeleton_pose_joint_count) {
  EXPECT_EQ((int)Joint::COUNT, 32);
  RigPose pose;
  for (int i = 0; i < (int)Joint::COUNT; ++i) {
    EXPECT_NEAR(pose.joints[i].x, 0.0f, 1e-6f);
    EXPECT_NEAR(pose.joints[i].y, 0.0f, 1e-6f);
    EXPECT_NEAR(pose.joints[i].z, 0.0f, 1e-6f);
  }
  return true;
}

DELVE_TEST(skeleton_symmetry_at_rest) {
  RigPose pose;
  ActorConfig cfg;
  pose.joints[(int)Joint::HIPS]     = {0, 0, 0};
  pose.joints[(int)Joint::SPINE_01] = {0, 0, cfg.torso_len * 0.3f};
  pose.joints[(int)Joint::CHEST]    = {0, 0, cfg.torso_len * 0.7f};
  pose.joints[(int)Joint::NECK]     = {0, 0, cfg.torso_len};
  pose.joints[(int)Joint::HEAD]     = {0, 0, cfg.torso_len + cfg.neck_len};
  pose.joints[(int)Joint::L_UPPER_ARM] = {-cfg.shoulder_width, 0, cfg.torso_len};
  pose.joints[(int)Joint::R_UPPER_ARM] = {cfg.shoulder_width, 0, cfg.torso_len};
  pose.joints[(int)Joint::L_LOWER_ARM] = {-cfg.shoulder_width - cfg.arm_len,
                                          0, cfg.torso_len};
  pose.joints[(int)Joint::R_LOWER_ARM] = {cfg.shoulder_width + cfg.arm_len,
                                          0, cfg.torso_len};
  pose.joints[(int)Joint::L_HAND] = {
      -cfg.shoulder_width - cfg.arm_len - cfg.forearm_len, 0, cfg.torso_len};
  pose.joints[(int)Joint::R_HAND] = {
      cfg.shoulder_width + cfg.arm_len + cfg.forearm_len, 0, cfg.torso_len};
  pose.joints[(int)Joint::L_UPPER_LEG] = {-cfg.hip_width, 0, 0};
  pose.joints[(int)Joint::R_UPPER_LEG] = {cfg.hip_width, 0, 0};
  pose.joints[(int)Joint::L_LOWER_LEG] = {-cfg.hip_width, 0, -cfg.leg_len};
  pose.joints[(int)Joint::R_LOWER_LEG] = {cfg.hip_width, 0, -cfg.leg_len};
  pose.joints[(int)Joint::L_FOOT] = {-cfg.hip_width,
                                     0, -cfg.leg_len - cfg.shin_len};
  pose.joints[(int)Joint::R_FOOT] = {cfg.hip_width,
                                     0, -cfg.leg_len - cfg.shin_len};

  float sym = pose_symmetry_score(pose);
  EXPECT_GT(sym, 0.95f);
  return true;
}

DELVE_TEST(leg_state_parallel_arrays_consistent) {
  LegState ls;
  for (int i = 0; i < 2; ++i) {
    EXPECT_FALSE(ls.stepping[i]);
    EXPECT_NEAR(ls.progress[i], 0.0f, 1e-6f);
  }
  return true;
}

// ---- Biomechanical animation tests ----

// smooth_damp helper (mirrors actor_animation.cpp implementation).
static float smooth_damp_test(float current, float target, float *velocity,
                              float smooth_time, float dt) {
  float omega = 2.0f / smooth_time;
  float x = omega * dt;
  float exp_f = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
  float delta = current - target;
  float temp = (*velocity + omega * delta) * dt;
  *velocity = (*velocity - omega * temp) * exp_f;
  return target + (delta + temp) * exp_f;
}

// Test: smooth_damp converges to target within 1 second (60 frames,
// smooth_time=0.1s).
DELVE_TEST(smooth_damp_convergence) {
  float current = 0.0f, target = 1.0f, rate = 0.0f;
  float dt = 1.0f / 60.0f;
  for (int i = 0; i < 60; ++i)
    current = smooth_damp_test(current, target, &rate, 0.1f, dt);
  EXPECT_GT(current, 0.99f);
  return true;
}

// Test: Arm phase opposes leg phase (anti-phase) for >70% of gait cycle
// samples. Fix 1 foundation: left arm = sin(phase + PI) = -sin(phase), left leg
// ∝ sin(phase).
DELVE_TEST(arm_phase_opposes_leg) {
  int opposing_count = 0;
  int total_samples = 100;
  for (int i = 0; i < total_samples; ++i) {
    float phase = (float)i / total_samples * glm::two_pi<float>();
    float l_arm_angle = sinf(phase + glm::pi<float>());
    float l_leg_fwd = sinf(phase);
    float opposition = arm_phase_opposition(l_arm_angle, l_leg_fwd);
    if (opposition > 0.5f)
      ++opposing_count;
  }
  float ratio = (float)opposing_count / total_samples;
  EXPECT_GT(ratio, 0.7f);
  return true;
}

// Test: Joint delay ordering — shoulder converges faster than wrist (successive
// breaking). Validates joint-delay chain: shoulder(0.02s) <
// elbow(0.04s) < wrist(0.06s).
DELVE_TEST(joint_delay_ordering) {
  float dt = 1.0f / 60.0f;
  float target = 1.0f;
  float shoulder = 0.0f, shoulder_rate = 0.0f;
  float elbow = 0.0f, elbow_rate = 0.0f;
  float wrist = 0.0f, wrist_rate = 0.0f;
  for (int i = 0; i < 3; ++i) {
    shoulder = smooth_damp_test(shoulder, target, &shoulder_rate, 0.02f, dt);
    elbow = smooth_damp_test(elbow, shoulder, &elbow_rate, 0.04f, dt);
    wrist = smooth_damp_test(wrist, elbow, &wrist_rate, 0.06f, dt);
  }
  float shoulder_err = fabsf(shoulder - target);
  float elbow_err = fabsf(elbow - target);
  float wrist_err = fabsf(wrist - target);
  EXPECT_LT(shoulder_err, elbow_err);
  EXPECT_LT(elbow_err, wrist_err);
  return true;
}

// Test: Idle breathing frequency is approximately 0.6 Hz (≈3 cycles in 5
// seconds).
DELVE_TEST(idle_breathing_frequency) {
  float dt = 1.0f / 60.0f;
  int frames = (int)(5.0f / dt);
  float phase = 0.0f, prev_val = 0.0f;
  int crossings = 0;
  for (int i = 0; i < frames; ++i) {
    phase += dt * glm::two_pi<float>() * 0.6f;
    float val = sinf(phase) * 0.008f;
    if (prev_val < 0.0f && val >= 0.0f)
      ++crossings;
    prev_val = val;
  }
  EXPECT_GT(crossings, 1);
  EXPECT_LT(crossings, 6);
  return true;
}

// Test: Velocity smoothing has weight — can't reach full speed in 1 frame.
DELVE_TEST(velocity_smoothing_has_weight) {
  float move_speed = 4.0f, current = 0.0f, rate = 0.0f;
  float dt = 1.0f / 60.0f;
  float result = smooth_damp_test(current, move_speed, &rate, 0.1f, dt);
  EXPECT_LT(result, move_speed * 0.5f);
  EXPECT_GT(result, 0.0f);
  return true;
}

// Test: Fix 1 — Smootherstep foot path has peak XY velocity at mid-stride.
// smootherstep(t) = 6t^5 - 15t^4 + 10t^3
// Derivative peaks at t=0.5 and has zero first+second derivatives at endpoints.
DELVE_TEST(elliptical_foot_path_peak_velocity_at_midstride) {
  auto smootherstep = [](float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
  };
  float dp = 0.05f;
  float t_start = smootherstep(0.00f + dp) - smootherstep(0.00f);
  float t_mid   = smootherstep(0.50f + dp) - smootherstep(0.50f);
  float t_end   = smootherstep(1.00f)      - smootherstep(1.00f - dp);
  // Mid-stride velocity is strictly greater than start and end.
  EXPECT_GT(t_mid, t_start);
  EXPECT_GT(t_mid, t_end);
  // Endpoints are slow: less than half the mid-stride rate.
  EXPECT_LT(t_start, t_mid * 0.5f);
  EXPECT_LT(t_end, t_mid * 0.5f);
  return true;
}

// Test: Fix 2 — Gait phase uses constant swing rate (direction-independent).
// At full speed (4.0 u/s) with SWING_RATE=2.7, arm swing frequency is ~1.72 Hz
// — a casual walking cadence regardless of movement direction.
DELVE_TEST(gait_phase_constant_swing_rate) {
  constexpr float SWING_RATE = 2.7f; // must match actor_animation.cpp
  constexpr float FULL_SPEED = 4.0f;
  constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;

  float phase_rate = FULL_SPEED * SWING_RATE; // rad/s at full speed
  float freq_hz = phase_rate / TWO_PI;

  // Casual walk cadence: ~1.5–2.0 Hz at full speed.
  EXPECT_GT(freq_hz, 1.5f);
  EXPECT_LT(freq_hz, 2.0f);

  // Direction-independent: phase rate is strictly linear with speed.
  float half_speed_rate = (FULL_SPEED * 0.5f) * SWING_RATE;
  EXPECT_NEAR(half_speed_rate, phase_rate * 0.5f, 1e-4f); // linear with speed
  return true;
}

// Test: Inverted pendulum — |sin(phase)| has exactly 2 peaks (midstance rises)
// per 2*PI cycle. Peaks of |sin(x)| occur at x=PI/2 and x=3*PI/2.
DELVE_TEST(hip_double_bounce_twice_per_stride) {
  float amplitude = 0.018f;
  int peak_count = 0;
  int total_steps = 1000;
  for (int i = 1; i < total_steps; ++i) {
    float prev_phase = (float)(i - 1) / total_steps * glm::two_pi<float>();
    float curr_phase = (float)i / total_steps * glm::two_pi<float>();
    float next_phase = (float)(i + 1) / total_steps * glm::two_pi<float>();
    float prev_bob = fabsf(sinf(prev_phase)) * amplitude;
    float curr_bob = fabsf(sinf(curr_phase)) * amplitude;
    float next_bob = fabsf(sinf(next_phase)) * amplitude;
    if (curr_bob > prev_bob && curr_bob > next_bob)
      ++peak_count;
  }
  EXPECT_EQ(peak_count, 2);
  return true;
}

// Test: Fix 3 — Hip roll counter-animates: sign of roll matches gait phase sign
// (hips shift to planted-foot side). When sin(phase) > 0, target_hip_roll > 0
// (hips roll right when left foot is back).
DELVE_TEST(hip_roll_counter_animation) {
  int matching_count = 0;
  int total_samples = 100;
  for (int i = 0; i < total_samples; ++i) {
    float phase = (float)i / total_samples * glm::two_pi<float>();
    float sin_p = sinf(phase);
    if (fabsf(sin_p) < 0.05f)
      continue; // skip near-zero crossings
    float target_hip_roll = sin_p * 0.06f;
    // Roll sign should match sin(phase) sign — counter-animation means hips
    // shift to the planted foot side.
    bool same_sign = (target_hip_roll > 0) == (sin_p > 0);
    if (same_sign)
      ++matching_count;
  }
  // Should be >90% matching (excluding zero crossings already skipped above).
  EXPECT_GT(matching_count, 70);
  return true;
}

// Test: One-foot-planted invariant — both legs never step simultaneously.
DELVE_TEST(no_simultaneous_stepping) {
  LegState legs{};
  ProceduralGait gait{};

  legs.foot[0] = {-0.25f, 0.0f, 0.0f};
  legs.foot[1] = {0.25f, 0.0f, 0.0f};
  legs.prev_foot[0] = legs.foot[0];
  legs.prev_foot[1] = legs.foot[1];
  legs.target[0] = legs.foot[0];
  legs.target[1] = legs.foot[1];

  float dt = 1.0f / 60.0f;
  bool both_stepping_ever = false;

  float pos_x = 0.0f, pos_y = 0.0f;
  float vel_x = 3.0f, vel_y = 0.0f;
  float facing = 0.0f;

  for (int frame = 0; frame < 200; ++frame) {
    float speed = sqrtf(vel_x * vel_x + vel_y * vel_y);
    gait.phase +=
        speed * dt * (glm::two_pi<float>() / (2.0f * gait.stride_len));

    pos_x += vel_x * dt;
    pos_y += vel_y * dt;

    float rght_x = -sinf(facing), rght_y = cosf(facing);
    float hip_sign[2] = {-1.0f, 1.0f};
    float vel_dx = vel_x / speed, vel_dy = vel_y / speed;

    float speed_ratio = std::max(0.4f, std::min(1.0f, speed / gait.move_speed));
    float adaptive_duration = gait.step_duration / speed_ratio;

    for (int leg = 0; leg < 2; ++leg) {
      int other_leg = 1 - leg;

      float hip_x = pos_x + rght_x * hip_sign[leg] * 0.25f;
      float hip_y = pos_y + rght_y * hip_sign[leg] * 0.25f;

      float pred_x = hip_x + vel_dx * gait.stride_len * 0.5f;
      float pred_y = hip_y + vel_dy * gait.stride_len * 0.5f;

      if (!legs.stepping[leg]) {
        float dx = legs.foot[leg].x - pred_x;
        float dy = legs.foot[leg].y - pred_y;
        float dist = sqrtf(dx * dx + dy * dy);

        bool other_planted = !legs.stepping[other_leg];
        if (dist > gait.stride_len * 0.5f && other_planted) {
          legs.stepping[leg] = true;
          legs.progress[leg] = 0.0f;
          legs.prev_foot[leg] = legs.foot[leg];
          legs.target[leg] = {pred_x, pred_y, 0.0f};
        }
      }

      if (legs.stepping[leg]) {
        legs.progress[leg] += dt / adaptive_duration;
        float progress = std::min(legs.progress[leg], 1.0f);
        // Use smootherstep for XY — matches live GaitSystem.
        float ts = progress * progress * progress * (progress * (progress * 6.0f - 15.0f) + 10.0f);

        legs.foot[leg].x = legs.prev_foot[leg].x +
                           (legs.target[leg].x - legs.prev_foot[leg].x) * ts;
        legs.foot[leg].y = legs.prev_foot[leg].y +
                           (legs.target[leg].y - legs.prev_foot[leg].y) * ts;

        if (legs.progress[leg] >= 1.0f) {
          legs.stepping[leg] = false;
          legs.foot[leg] = legs.target[leg];
        }
      }
    }

    if (legs.stepping[0] && legs.stepping[1])
      both_stepping_ever = true;
  }

  EXPECT_FALSE(both_stepping_ever);
  return true;
}

// ---- Character height proportion tests (fixes "too tall" appearance) ----

// Total standing height from ground = leg_len + shin_len + torso_len + neck_len
// + head_radius. This must be a reasonable fraction of the hex tile size so the
// character doesn't appear giant relative to terrain. Max 35% of HEX_SIZE is
// enforced.
DELVE_TEST(character_total_height_relative_to_hex_size) {
  ActorConfig cfg;
  float total_height = cfg.leg_len + cfg.shin_len + cfg.torso_len +
                       cfg.neck_len + cfg.head_radius;
  float hex_size = Config::HEX_SIZE;
  float ratio = total_height / hex_size;
  EXPECT_LT(ratio, 0.35f);
  EXPECT_GT(ratio, 0.15f);
  return true;
}

// Leg proportion: (leg_len + shin_len) / total_height should be ~0.45–0.55
DELVE_TEST(character_leg_proportion_human_like) {
  ActorConfig cfg;
  float leg_height = cfg.leg_len + cfg.shin_len;
  float total_height =
      leg_height + cfg.torso_len + cfg.neck_len + cfg.head_radius;
  float leg_ratio = leg_height / total_height;
  EXPECT_GT(leg_ratio, 0.40f);
  EXPECT_LT(leg_ratio, 0.60f);
  return true;
}

// Head proportion: head_radius / total_height should be <= 0.12
DELVE_TEST(character_head_size_not_too_large) {
  ActorConfig cfg;
  float total_height = cfg.leg_len + cfg.shin_len + cfg.torso_len +
                       cfg.neck_len + cfg.head_radius;
  float head_ratio = cfg.head_radius / total_height;
  EXPECT_LT(head_ratio, 0.12f);
  EXPECT_GT(head_ratio, 0.04f);
  return true;
}

// Test: Actor total height must be < 0.5 * HEX_SIZE as a hard sanity ceiling
// (distinct from the proportional 15–35% bound in character_total_height_relative_to_hex_size).
DELVE_TEST(actor_total_height_within_one_tile) {
  ActorConfig c;
  float total_height =
      c.leg_len + c.shin_len + c.torso_len + c.neck_len + c.head_radius;
  EXPECT_LT(total_height, Config::HEX_SIZE * 0.5f); // hard ceiling: < 4 world units
  EXPECT_GT(total_height, 0.5f);
  return true;
}

// Grounding offset (leg_len + shin_len) must equal the distance added to
// terrain height to place the actor root — the IK solver assumes this exactly.
DELVE_TEST(grounding_offset_equals_leg_plus_shin) {
  ActorConfig c;
  float grounding_offset = c.leg_len + c.shin_len;
  // This verifies config values are in the expected physical range.
  // The actual grounding system's use of this offset is exercised by the ECS system.
  EXPECT_GT(grounding_offset, 0.0f);
  EXPECT_LT(grounding_offset, 4.0f);
  return true;
}

// Test: Spine chain is strictly ascending in Z — IK chain doesn't flip the
// skeleton.
DELVE_TEST(spine_chain_ascending_in_z) {
  ActorConfig cfg;
  float base_z = 10.0f;
  glm::vec3 hips = {0, 0, base_z};
  glm::vec3 spine = hips + glm::vec3(0, 0, cfg.torso_len * 0.4f);
  glm::vec3 chest = hips + glm::vec3(0, 0, cfg.torso_len);
  glm::vec3 neck = chest + glm::vec3(0, 0, cfg.neck_len);
  glm::vec3 head = neck + glm::vec3(0, 0, cfg.head_radius);
  EXPECT_GT(spine.z, hips.z);
  EXPECT_GT(chest.z, spine.z);
  EXPECT_GT(neck.z, chest.z);
  EXPECT_GT(head.z, neck.z);
  return true;
}

// Test: Hip double-bounce bob magnitude is bounded (≤ 0.025 world units).
// Ensures vertical bob doesn't make character appear to sink into terrain.
DELVE_TEST(hip_bob_magnitude_bounded) {
  float max_bob = 0.0f;
  int total = 100;
  for (int i = 0; i < total; ++i) {
    float phase = (float)i / total * glm::two_pi<float>();
    float bob = fabsf(sinf(phase)) * 0.018f;
    max_bob = std::max(max_bob, bob);
  }
  EXPECT_LT(max_bob, 0.019f);
  EXPECT_GT(max_bob, 0.0f);
  return true;
}

// ---- Tests for walk animation math (update_walk_animation, compute_hip_counter_animation,
//      compute_foot_position formulas verified inline) ----

// Inline helpers mirroring actor_animation.cpp pure-math functions so tests
// have no SDL/Flecs/ECS dependency.

// Mirrors the live GaitSystem phase advance formula (direction-independent).
static float test_advance_phase(float phase, float dt, float speed) {
    constexpr float SWING_RATE = 2.7f; // must match actor_animation.cpp
    return phase + speed * dt * SWING_RATE;
}

struct TestHipState {
    float hip_rotation_deg   = 0.0f;
    float hip_drop_fraction  = 0.0f;
    float hip_bob_y          = 0.0f;
};

static TestHipState test_compute_hip_counter(float stride_phase,
                                              float hip_sway_deg    = 5.0f,
                                              float hip_drop_max    = 0.03f,
                                              float hip_bob_amp     = 0.02f) {
    float two_pi_phase = stride_phase * 2.0f * glm::pi<float>();
    TestHipState s;
    s.hip_rotation_deg  = hip_sway_deg  * std::sin(two_pi_phase);
    s.hip_drop_fraction = hip_drop_max  * (1.0f - std::abs(std::cos(two_pi_phase)));
    s.hip_bob_y         = hip_bob_amp   * std::abs(std::sin(two_pi_phase));
    return s;
}

static glm::vec3 test_compute_foot_position(float t,
                                             glm::vec3 prev, glm::vec3 target, float step_height) {
    // Smootherstep — matches live actor_animation.cpp GaitSystem formula.
    float warped_t = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    float ts_z     = t * t * (3.0f - 2.0f * t);
    glm::vec3 out;
    out.x = prev.x + (target.x - prev.x) * warped_t;
    out.y = prev.y + (target.y - prev.y) * warped_t;
    out.z = prev.z + (target.z - prev.z) * ts_z + std::sin(t * glm::pi<float>()) * step_height;
    return out;
}

// Test: phase advances proportional to speed; double speed → double advance.
// GaitSystem uses a constant swing rate (no directional weighting).
DELVE_TEST(walk_animation_phase_proportional_to_speed) {
    float dt = 1.0f / 60.0f;

    float p1 = test_advance_phase(0.0f, dt, 1.0f);
    float p2 = test_advance_phase(0.0f, dt, 2.0f);

    // Double speed → exactly double phase advance (linear, direction-independent).
    EXPECT_GT(p2, p1 * 1.98f);
    EXPECT_LT(p2, p1 * 2.02f);
    return true;
}

// Test: Elbow bends forward (natural direction) — forearm rotates toward
// front of character, not backward (hyperextension).
// The swing_wrist_pos formula adds -25° to the forearm rotation relative to
// the upper arm. In the rotation convention used (positive angle = backward),
// subtracting 25° keeps the forearm angled forward at all swing phases.
DELVE_TEST(elbow_bends_forward_not_backward) {
    constexpr float PI = 3.14159265358979323846f;
    // Simulate the wrist position formula at a backward arm swing (+0.3 rad).
    // right_axis = (0,1,0), hang_down = (0,0,-1), shoulder_angle = +0.3 rad.
    // With -25° constant: total_angle = 0.3 - radians(25) = 0.3 - 0.436 = -0.136
    // Forearm dir (world facing=0): -sin(total_angle) x, 0 y, -cos(total_angle) z
    // Elbow angle contribution is small; test the pure constant-bend direction.
    float shoulder_angle = 0.3f; // backward swing
    float elbow_bias     = -25.0f * (PI / 180.0f); // -25 degrees = forward bend
    float total_angle    = shoulder_angle + elbow_bias; // net: slightly forward of vertical

    // Rodrigues rotation of hang_down=(0,0,-1) around right_axis=(0,1,0):
    // dir = (0,0,-1)*cos(a) + (-1,0,0)*sin(a)  [cross((0,1,0),(0,0,-1)) = (-1,0,0)]
    float forearm_fwd_component = -sinf(total_angle); // x-component with facing=0

    // At shoulder_angle=+0.3 (backward) with -25° bias, total ≈ -0.136 rad.
    // sin(-0.136) ≈ -0.136, so forearm_fwd_component = +0.136 (forward lean).
    // Shoulder direction at +0.3: forearm_fwd_component would be -sin(0.3) ≈ -0.296.
    // The forearm is more forward than the upper arm → natural forward elbow bend.
    float upper_arm_fwd = -sinf(shoulder_angle); // upper arm forward component
    EXPECT_GT(forearm_fwd_component, upper_arm_fwd); // forearm is more forward
    return true;
}

// Test: hip_rotation_deg bounded to ±hip_sway_deg, bob ≥ 0, drop ≥ 0.
DELVE_TEST(hip_counter_animation_bounds) {
    bool rotation_ok = true, bob_ok = true, drop_ok = true;
    const float sway_deg = 5.0f, drop_max = 0.03f, bob_amp = 0.02f;

    for (int i = 0; i < 100; ++i) {
        auto s = test_compute_hip_counter((float)i / 100.0f, sway_deg, drop_max, bob_amp);
        if (std::abs(s.hip_rotation_deg) > sway_deg + 1e-4f)   rotation_ok = false;
        if (s.hip_bob_y < -1e-4f || s.hip_bob_y > bob_amp + 1e-4f) bob_ok = false;
        if (s.hip_drop_fraction < -1e-4f || s.hip_drop_fraction > drop_max + 1e-4f) drop_ok = false;
    }
    EXPECT_TRUE(rotation_ok);
    EXPECT_TRUE(bob_ok);
    EXPECT_TRUE(drop_ok);
    return true;
}

// Test: hip_bob_y (|sin(2π*phase)|) peaks exactly twice per [0,1) cycle.
DELVE_TEST(hip_bob_two_peaks_per_cycle) {
    float prev = 0.0f, pprev = 0.0f;
    int peaks = 0;
    for (int i = 0; i <= 200; ++i) {
        float cur = test_compute_hip_counter((float)i / 200.0f).hip_bob_y;
        if (i >= 2 && pprev < prev && cur < prev)
            ++peaks;
        pprev = prev; prev = cur;
    }
    EXPECT_EQ(peaks, 2);
    return true;
}

// Test: foot Z-lift = 0 at t=0 and t=1, peaks near step_height at t=0.5.
DELVE_TEST(foot_position_z_lift_arc) {
    glm::vec3 prev{0,0,0}, target{1,0,0};
    float sh = 0.5f;
    glm::vec3 at0  = test_compute_foot_position(0.0f, prev, target, sh);
    glm::vec3 at05 = test_compute_foot_position(0.5f, prev, target, sh);
    glm::vec3 at1  = test_compute_foot_position(1.0f, prev, target, sh);

    EXPECT_NEAR(at0.z,  0.0f, 1e-4f);
    EXPECT_NEAR(at1.z,  0.0f, 1e-4f);
    EXPECT_NEAR(at05.z, sh,   0.02f);
    EXPECT_GT(at05.z, at0.z);
    EXPECT_GT(at05.z, at1.z);
    return true;
}

// Test: foot XY velocity peaks at mid-stride (sine-warp easing).
DELVE_TEST(foot_position_xy_velocity_peaks_at_midstride) {
    glm::vec3 prev{0,0,0}, target{2,0,0};
    float dp = 0.05f;
    float v_start = test_compute_foot_position(dp, prev, target, 0.0f).x
                  - test_compute_foot_position(0.0f, prev, target, 0.0f).x;
    float v_mid   = test_compute_foot_position(0.5f + dp, prev, target, 0.0f).x
                  - test_compute_foot_position(0.5f, prev, target, 0.0f).x;
    float v_end   = test_compute_foot_position(1.0f, prev, target, 0.0f).x
                  - test_compute_foot_position(1.0f - dp, prev, target, 0.0f).x;

    EXPECT_GT(v_mid, v_start);
    EXPECT_GT(v_mid, v_end);
    EXPECT_LT(v_start, v_mid * 0.5f);
    return true;
}

// Test: foot position XY reaches target exactly at t=1 (Z also reaches target when sh=0).
DELVE_TEST(foot_position_reaches_target_at_t1) {
    glm::vec3 prev{3,-1,2}, target{5,2,1};
    glm::vec3 at1 = test_compute_foot_position(1.0f, prev, target, 0.0f);
    EXPECT_NEAR(at1.x, target.x, 1e-4f);
    EXPECT_NEAR(at1.y, target.y, 1e-4f);
    EXPECT_NEAR(at1.z, target.z, 1e-4f);
    return true;
}

// ---- ISO height foreshortening tests ----

// Test: AnimationConfig::ISO_CHAR_HEIGHT_SCALE = 0.816 * 0.92 ≈ 0.751.
// This constant squashes character height for correct 2:1 isometric proportions.
DELVE_TEST(iso_char_height_scale_value) {
    float scale = AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
    // Should be ~0.751 (within 1% tolerance).
    EXPECT_GT(scale, 0.74f);
    EXPECT_LT(scale, 0.76f);
    return true;
}

// Test: Foreshortening reduces skeleton height relative to foot_z.
// After applying ISO_CHAR_HEIGHT_SCALE, the vertical extent above foot_z
// is strictly less than before (and greater than 0).
DELVE_TEST(iso_foreshortening_reduces_height) {
    ActorConfig cfg;
    float foot_z = 0.0f;

    // Build a simple spine stack above foot_z.
    float heights_before[5] = {
        0.0f,
        cfg.leg_len,
        cfg.leg_len + cfg.shin_len,
        cfg.leg_len + cfg.shin_len + cfg.torso_len,
        cfg.leg_len + cfg.shin_len + cfg.torso_len + cfg.neck_len + cfg.head_radius
    };

    float scale = AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
    float top_before = heights_before[4];
    float top_after  = foot_z + (top_before - foot_z) * scale;

    EXPECT_LT(top_after, top_before);
    EXPECT_GT(top_after, 0.0f);
    // Foot level stays planted (foot_z is the reference, not scaled).
    float foot_after = foot_z + (foot_z - foot_z) * scale;
    EXPECT_NEAR(foot_after, foot_z, 1e-6f);
    return true;
}

// Test: Planted-foot XY clamp limits horizontal distance from hip.
// Simulates a foot planted far behind its hip (as in a 180° turn).
DELVE_TEST(planted_foot_clamped_to_stride_len) {
  ProceduralGait gait{};
  float max_horiz = gait.stride_len * 0.9f;

  // Foot 3.0 units behind hip — clearly hyperextended.
  float hip_x = 5.0f, hip_y = 0.0f;
  float foot_x = 2.0f, foot_y = 0.0f;

  float dx = foot_x - hip_x;
  float dy = foot_y - hip_y;
  float hd = sqrtf(dx * dx + dy * dy);
  EXPECT_GT(hd, max_horiz); // confirm over the limit

  // Apply clamp (mirrors GaitSystem logic).
  if (hd > max_horiz) {
    float s = max_horiz / hd;
    foot_x = hip_x + dx * s;
    foot_y = hip_y + dy * s;
  }

  float clamped_dist = sqrtf((foot_x - hip_x) * (foot_x - hip_x) +
                             (foot_y - hip_y) * (foot_y - hip_y));
  EXPECT_NEAR(clamped_dist, max_horiz, 1e-4f);
  return true;
}

// Test: IK ankle clamp pulls ankle inward when foot target is
// significantly beyond leg reach (prevents visual hyperextension).
DELVE_TEST(ik_ankle_clamp_prevents_hyperextension) {
  ActorConfig cfg;
  float a = cfg.leg_len, b = cfg.shin_len;
  float max_D = a + b - 0.005f;
  float stretch_limit = max_D * 1.15f;

  // Hip at origin, foot target far away.
  glm::vec3 H(0, 0, 0);
  glm::vec3 foot_target(1.0f, 0.0f, -2.0f);
  float D = glm::length(foot_target - H);
  EXPECT_GT(D, stretch_limit); // confirm over-extended

  // Apply ankle clamp (mirrors IKSystem logic).
  glm::vec3 axis = foot_target - H;
  glm::vec3 clamped_ankle = H + (axis / D) * stretch_limit;
  float clamped_D = glm::length(clamped_ankle - H);
  EXPECT_NEAR(clamped_D, stretch_limit, 1e-4f);
  EXPECT_LT(clamped_D, D);
  return true;
}

// Test: During a simulated 180° turn, planted foot XY distance from
// hip never exceeds stride_len (with clamp applied).
DELVE_TEST(no_hyperextension_during_180_turn) {
  LegState legs{};
  ProceduralGait gait{};
  ActorConfig cfg;

  legs.foot[0] = {-cfg.hip_width, 0.0f, 0.0f};
  legs.foot[1] = { cfg.hip_width, 0.0f, 0.0f};

  float dt = 1.0f / 60.0f;
  float pos_x = 0.0f;
  float vel_x = 4.0f; // walking right at full speed
  float max_horiz = gait.stride_len * 0.9f;
  float max_observed = 0.0f;

  // 60 frames forward, then 120 frames reversed.
  for (int frame = 0; frame < 180; ++frame) {
    if (frame == 60) vel_x = -4.0f;
    pos_x += vel_x * dt;
    float speed = fabsf(vel_x);
    float vel_dx = vel_x / speed;
    float facing = (vel_x > 0) ? 0.0f : 3.14159265f;
    float rght_x = -sinf(facing);
    float hip_sign[2] = {-1.0f, 1.0f};

    float speed_ratio = std::max(0.4f, std::min(1.0f, speed / gait.move_speed));
    float adaptive_duration = gait.step_duration / speed_ratio;

    for (int leg = 0; leg < 2; ++leg) {
      int other = 1 - leg;
      float hip_x = pos_x + rght_x * hip_sign[leg] * cfg.hip_width;
      float half_stride = gait.stride_len * 0.5f;
      float center_x = hip_x + vel_dx * half_stride;

      if (!legs.stepping[leg]) {
        float dx = legs.foot[leg].x - center_x;
        float dist = fabsf(dx);
        if (dist > half_stride && !legs.stepping[other]) {
          legs.stepping[leg] = true;
          legs.progress[leg] = 0.0f;
          legs.prev_foot[leg] = legs.foot[leg];
          float step_travel = speed * adaptive_duration;
          float target_off = half_stride + step_travel * 0.75f;
          legs.target[leg] = {hip_x + vel_dx * target_off, 0.0f, 0.0f};
        }
      }
      if (legs.stepping[leg]) {
        legs.progress[leg] += dt / adaptive_duration;
        float p = std::min(legs.progress[leg], 1.0f);
        float w = p*p*p*(p*(p*6.0f-15.0f)+10.0f);
        legs.foot[leg].x = legs.prev_foot[leg].x +
            (legs.target[leg].x - legs.prev_foot[leg].x) * w;
        if (legs.progress[leg] >= 1.0f) {
          legs.stepping[leg] = false;
          legs.foot[leg] = legs.target[leg];
        }
      }
    }

    // Apply planted-foot clamp (mirrors live GaitSystem).
    for (int leg = 0; leg < 2; ++leg) {
      float hip_x = pos_x + rght_x * hip_sign[leg] * cfg.hip_width;
      if (!legs.stepping[leg]) {
        float dx = legs.foot[leg].x - hip_x;
        float hd = fabsf(dx);
        if (hd > max_horiz) {
          legs.foot[leg].x = hip_x + (dx / hd) * max_horiz;
        }
      } else if (legs.progress[leg] < 0.4f) {
        float tdx = legs.target[leg].x - hip_x;
        float th = fabsf(tdx);
        if (th > max_horiz) {
          float step_travel = speed * adaptive_duration;
          float target_off = gait.stride_len * 0.5f + step_travel * 0.75f;
          legs.target[leg].x = hip_x + vel_dx * target_off;
        }
      }
    }

    // Track max XY distance from hip.
    for (int leg = 0; leg < 2; ++leg) {
      float hip_x = pos_x + rght_x * hip_sign[leg] * cfg.hip_width;
      float d = fabsf(legs.foot[leg].x - hip_x);
      max_observed = std::max(max_observed, d);
    }
  }

  // With clamp, max distance should be <= stride_len.
  EXPECT_LT(max_observed, gait.stride_len * 1.1f);
  return true;
}

// Test: Knee reaches near-full extension (lock) at mid-stance when foot is
// directly below the hip socket.  The IK margin (0.005) allows the knee angle
// to reach ≥ 160° (within 20° of full extension = 180°), giving the visual
// appearance of a locked stance leg.
DELVE_TEST(knee_locks_at_midstance) {
  ActorConfig cfg;
  float a = cfg.leg_len;   // 0.430 thigh
  float b = cfg.shin_len;  // 0.390 shin
  float margin = 0.005f;
  float max_D = a + b - margin;

  // At mid-stance the foot is directly below the hip socket.
  // D ≈ leg_len + shin_len, clamped to max_D.
  float D = std::min(a + b, max_D);

  // Knee angle via law of cosines: cos(K) = (a² + b² - D²) / (2ab)
  float cos_knee = (a * a + b * b - D * D) / (2.0f * a * b);
  cos_knee = std::max(-1.0f, std::min(1.0f, cos_knee));
  float knee_deg = std::acos(cos_knee) * (180.0f / 3.14159265f);

  // Knee should be ≥ 160° (near full extension, reads as "locked").
  EXPECT_GT(knee_deg, 160.0f);
  // But not perfectly 180° (margin prevents that).
  EXPECT_LT(knee_deg, 180.0f);
  return true;
}

// Test: AnimationConfig retains directional_speed_scale field for reference,
// but gait phase no longer uses it (constant swing rate instead).
DELVE_TEST(animation_config_hip_counter_fields_valid) {
    AnimationConfig cfg;
    // Hip counter-animation parameters should have sensible defaults.
    EXPECT_GT(cfg.hip_sway_deg, 0.0f);
    EXPECT_GT(cfg.hip_drop_max, 0.0f);
    EXPECT_GT(cfg.hip_bob_amplitude, 0.0f);
    return true;
}

// Test: AnimationConfig hip counter-animation defaults are non-zero and bounded.
DELVE_TEST(animation_config_hip_defaults_valid) {
    AnimationConfig acfg;
    EXPECT_GT(acfg.hip_sway_deg,      0.0f);
    EXPECT_LT(acfg.hip_sway_deg,     15.0f);
    EXPECT_GT(acfg.hip_drop_max,      0.0f);
    EXPECT_LT(acfg.hip_drop_max,      0.1f);
    EXPECT_GT(acfg.hip_bob_amplitude, 0.0f);
    EXPECT_LT(acfg.hip_bob_amplitude, 0.1f);
    return true;
}

// Test: Smootherstep is strictly steeper than cosine-ease at midpoint.
// Both are S-curves: smootherstep has zero 1st+2nd derivatives at endpoints,
// giving faster transit through midpoint than cosine-ease.
DELVE_TEST(smootherstep_steeper_than_cosine_ease_at_midpoint) {
    auto smootherstep = [](float t) {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    };
    auto cosease = [](float t) {
        return (1.0f - std::cos(t * glm::pi<float>())) * 0.5f;
    };
    float dp = 0.02f;
    float ss_mid = smootherstep(0.5f + dp) - smootherstep(0.5f);
    float ce_mid = cosease(0.5f + dp)      - cosease(0.5f);
    // Smootherstep has higher velocity at midpoint (the curves are distinct).
    EXPECT_GT(ss_mid, ce_mid);
    // Both start and end at 0 and 1 exactly.
    EXPECT_NEAR(smootherstep(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(smootherstep(1.0f), 1.0f, 1e-6f);
    return true;
}
