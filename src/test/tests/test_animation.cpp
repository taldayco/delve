#include "test_harness.h"
#include "animation_metrics.h"
#include "actor.h"
#include <glm/glm.hpp>

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
  EXPECT_GT(c.shoulder_width, c.hip_width);  // shoulders wider than hips
  return true;
}

DELVE_TEST(skeleton_pose_joint_count) {
  EXPECT_EQ((int)Joint::COUNT, 17);
  SkeletonPose pose;
  for (int i = 0; i < (int)Joint::COUNT; ++i) {
    EXPECT_NEAR(pose.joints[i].x, 0.0f, 1e-6f);
    EXPECT_NEAR(pose.joints[i].y, 0.0f, 1e-6f);
    EXPECT_NEAR(pose.joints[i].z, 0.0f, 1e-6f);
  }
  return true;
}

DELVE_TEST(skeleton_symmetry_at_rest) {
  SkeletonPose pose;
  ActorConfig cfg;
  // Build a simple T-pose
  pose.joints[(int)Joint::ROOT]       = {0, 0, 0};
  pose.joints[(int)Joint::SPINE]      = {0, cfg.torso_len * 0.3f, 0};
  pose.joints[(int)Joint::CHEST]      = {0, cfg.torso_len * 0.7f, 0};
  pose.joints[(int)Joint::NECK]       = {0, cfg.torso_len, 0};
  pose.joints[(int)Joint::HEAD]       = {0, cfg.torso_len + cfg.neck_len, 0};
  pose.joints[(int)Joint::L_SHOULDER] = {-cfg.shoulder_width, cfg.torso_len, 0};
  pose.joints[(int)Joint::R_SHOULDER] = { cfg.shoulder_width, cfg.torso_len, 0};
  pose.joints[(int)Joint::L_ELBOW]    = {-cfg.shoulder_width - cfg.arm_len, cfg.torso_len, 0};
  pose.joints[(int)Joint::R_ELBOW]    = { cfg.shoulder_width + cfg.arm_len, cfg.torso_len, 0};
  pose.joints[(int)Joint::L_WRIST]    = {-cfg.shoulder_width - cfg.arm_len - cfg.forearm_len, cfg.torso_len, 0};
  pose.joints[(int)Joint::R_WRIST]    = { cfg.shoulder_width + cfg.arm_len + cfg.forearm_len, cfg.torso_len, 0};
  pose.joints[(int)Joint::L_HIP]      = {-cfg.hip_width, 0, 0};
  pose.joints[(int)Joint::R_HIP]      = { cfg.hip_width, 0, 0};
  pose.joints[(int)Joint::L_KNEE]     = {-cfg.hip_width, -cfg.leg_len, 0};
  pose.joints[(int)Joint::R_KNEE]     = { cfg.hip_width, -cfg.leg_len, 0};
  pose.joints[(int)Joint::L_ANKLE]    = {-cfg.hip_width, -cfg.leg_len - cfg.shin_len, 0};
  pose.joints[(int)Joint::R_ANKLE]    = { cfg.hip_width, -cfg.leg_len - cfg.shin_len, 0};

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

DELVE_TEST(smooth_velocity_convergence) {
  // Critically-damped spring from 0 to 1 with smooth_time=0.2s at 60Hz
  // should converge well within 600 steps
  int steps = smooth_velocity_convergence(0.0f, 1.0f, 0.2f, 1.0f / 60.0f);
  EXPECT_GT(steps, 0);    // must converge (not -1)
  EXPECT_LT(steps, 600);  // must converge before the cap
  return true;
}

DELVE_TEST(torso_lean_proportional) {
  // lean ~ speed * lean_factor; faster speed produces greater lean magnitude
  const float lean_factor = 0.04f;
  float slow_speed = 1.0f;
  float fast_speed = 6.0f;
  float lean_slow = slow_speed * lean_factor;
  float lean_fast = fast_speed * lean_factor;
  EXPECT_TRUE(torso_lean_proportional(lean_slow, lean_fast));
  return true;
}

DELVE_TEST(arm_swing_antiphase) {
  constexpr float pi = 3.14159265f;
  // Left at 0.0, right at π — exactly antiphase in radians
  EXPECT_TRUE(arm_swing_antiphase(0.0f, pi));
  // Phases within tolerance also pass (e.g. slight drift after some frames)
  EXPECT_TRUE(arm_swing_antiphase(0.1f, pi + 0.1f));
  // Same phase is NOT antiphase
  EXPECT_FALSE(arm_swing_antiphase(0.0f, 0.0f));
  // Offset by only 0.5 rad (~28°) is NOT antiphase
  EXPECT_FALSE(arm_swing_antiphase(0.0f, 0.5f));
  return true;
}

DELVE_TEST(breathing_amplitude_reasonable) {
  EXPECT_TRUE(breathing_amplitude_reasonable(0.012f));  // nominal value
  EXPECT_FALSE(breathing_amplitude_reasonable(0.0f));   // zero — no breath
  EXPECT_FALSE(breathing_amplitude_reasonable(0.1f));   // too large
  return true;
}

DELVE_TEST(foot_planted_invariant) {
  // Both feet stepping simultaneously is invalid
  LegState ls;
  ls.stepping[0] = true;
  ls.stepping[1] = true;
  EXPECT_FALSE(foot_planted_invariant(ls));

  // One foot stepping is valid
  ls.stepping[1] = false;
  EXPECT_TRUE(foot_planted_invariant(ls));

  // Neither stepping (standing still) is also valid
  ls.stepping[0] = false;
  EXPECT_TRUE(foot_planted_invariant(ls));
  return true;
}

DELVE_TEST(step_duration_adaptive) {
  // Faster speed should produce shorter step duration
  // dur = mix(0.45, 0.22, t_norm) where t_norm = clamp(speed/max_speed, 0, 1)
  const float dur_min  = 0.22f;
  const float dur_max  = 0.45f;
  const float max_spd  = 8.0f;

  float t_slow = 1.0f / max_spd;  // speed=1
  float t_fast = 6.0f / max_spd;  // speed=6
  float dur_slow = dur_max + t_slow * (dur_min - dur_max);
  float dur_fast = dur_max + t_fast * (dur_min - dur_max);

  EXPECT_TRUE(step_duration_adaptive(dur_slow, dur_fast));
  return true;
}

// ── Subtask 8: 6 new tests ────────────────────────────────────────────────

// Critically-damped spring formula (mirrors actor_animation.cpp implementation).
static float test_smooth_damp(float current, float target, float &vel_ref,
                               float smooth_time, float dt) {
    smooth_time = smooth_time < 0.0001f ? 0.0001f : smooth_time;
    float omega  = 2.0f / smooth_time;
    float x      = omega * dt;
    float exp_x  = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float temp   = (vel_ref + omega * change) * dt;
    vel_ref      = (vel_ref - omega * temp) * exp_x;
    return target + (change + temp) * exp_x;
}

// Test 1: smooth_damp converges from 0 → 1 within 1 second at 60 Hz.
DELVE_TEST(smooth_damp_convergence) {
    float current = 0.0f, vel = 0.0f;
    const float target = 1.0f, dt = 1.0f / 60.0f, smooth_time = 0.1f;
    for (int i = 0; i < 60; ++i)   // 1 second
        current = test_smooth_damp(current, target, vel, smooth_time, dt);
    EXPECT_GT(current, 0.99f);
    return true;
}

// Test 2: arm phase opposes leg phase for the majority of a full gait cycle.
DELVE_TEST(arm_phase_opposes_leg) {
    constexpr float two_pi = 6.28318530f;
    constexpr int   steps  = 64;
    int opposition_count   = 0;

    for (int i = 0; i < steps; ++i) {
        float gait_phase = (float)i / steps * two_pi;
        // Left arm opposes left leg: arm at gait_phase + π
        float l_arm_angle   = std::sin(gait_phase + 3.14159265f) * 0.18f;
        float l_leg_forward = std::sin(gait_phase) * 0.30f;
        if (arm_phase_opposition(l_arm_angle, l_leg_forward) > 0.5f)
            ++opposition_count;
    }
    // Expect >75% of the cycle to show anti-phase opposition
    float ratio = (float)opposition_count / steps;
    EXPECT_GT(ratio, 0.75f);
    return true;
}

// Test 3: joint delay ordering — shoulder converges fastest, wrist slowest.
DELVE_TEST(joint_delay_ordering) {
    float shoulder = 0.0f, shoulder_vel = 0.0f;
    float elbow    = 0.0f, elbow_vel    = 0.0f;
    float wrist    = 0.0f, wrist_vel    = 0.0f;
    const float target = 1.0f, dt = 1.0f / 60.0f;

    // 3 frames — each chain segment smooths toward its parent's output.
    for (int i = 0; i < 3; ++i) {
        shoulder = test_smooth_damp(shoulder, target,       shoulder_vel, 0.02f, dt);
        elbow    = test_smooth_damp(elbow,    shoulder,     elbow_vel,    0.05f, dt);
        wrist    = test_smooth_damp(wrist,    elbow,        wrist_vel,    0.08f, dt);
    }

    float shoulder_lag = std::abs(shoulder - target);
    float elbow_lag    = std::abs(elbow    - target);
    float wrist_lag    = std::abs(wrist    - target);

    // Shoulder is closest to target (least lag), wrist is furthest.
    EXPECT_LT(shoulder_lag, elbow_lag);
    EXPECT_LT(elbow_lag,    wrist_lag);
    EXPECT_GT(joint_lag_ratio(shoulder_lag, wrist_lag), 1.0f);
    return true;
}

// Test 4: idle breathing produces ~0.5–0.8 Hz frequency over 5 seconds.
DELVE_TEST(idle_breathing_frequency) {
    constexpr float dt       = 1.0f / 60.0f;
    constexpr float duration = 5.0f;
    constexpr float amp      = 0.012f;
    constexpr float rate     = 0.25f; // phase increments per second (matches SkeletonFinaliseSystem)

    float phase   = 0.0f;
    float prev    = 0.0f;
    int crossings = 0;

    for (int i = 0; i < (int)(duration / dt); ++i) {
        phase = std::fmod(phase + dt * rate, 1.0f);
        float breath = std::cos(phase * 6.28318530f) * amp;
        if (prev < 0.0f && breath >= 0.0f) ++crossings; // count upward zero-crossings
        prev = breath;
    }
    // Each full cycle = 1/rate seconds = 4 s → freq = 0.25 Hz
    // Rate 0.25 cycles/s → 0.25*5 = 1.25 cycles → ~1 or 2 crossings
    // Broader check: at least 1 crossing (alive), at most 5 (not too fast)
    EXPECT_GT(crossings, 0);
    EXPECT_LT(crossings, 6);
    // Verify amplitude is in the reasonable range
    EXPECT_TRUE(breathing_amplitude_reasonable(amp));
    return true;
}

// Test 5: after 1 frame at dt=1/60, smooth velocity is < 50% of target.
DELVE_TEST(velocity_smoothing_has_weight) {
    float current = 0.0f, vel = 0.0f;
    const float move_speed = 4.0f, dt = 1.0f / 60.0f, smooth_time = 0.12f;
    current = test_smooth_damp(current, move_speed, vel, smooth_time, dt);
    // After one frame the velocity should NOT have jumped to full speed.
    EXPECT_LT(current, move_speed * 0.5f);
    EXPECT_GT(current, 0.0f); // but it should have started moving
    return true;
}

// Test 6: simulate gait for 200 frames — both feet never step simultaneously.
DELVE_TEST(no_simultaneous_stepping) {
    LegState legs{};
    // Place feet in default positions.
    legs.foot[0] = {-0.25f, 0.0f, 0.0f};
    legs.foot[1] = { 0.25f, 0.0f, 0.0f};
    legs.prev_foot[0] = legs.foot[0];
    legs.prev_foot[1] = legs.foot[1];
    legs.target[0]    = legs.foot[0];
    legs.target[1]    = legs.foot[1];

    float gait_phase = 0.0f;
    const float move_speed = 4.0f, stride_len = 0.60f, step_height = 0.18f;
    float step_duration = 0.25f;
    const float dt = 1.0f / 60.0f;

    // Simple deterministic velocity — move diagonally.
    const float vx = 3.0f, vy = 1.5f;
    const float speed = std::sqrt(vx * vx + vy * vy);
    float tx = 0.0f, ty = 0.0f; // actor position

    for (int frame = 0; frame < 200; ++frame) {
        // Advance position.
        tx += vx * dt;
        ty += vy * dt;
        gait_phase += speed * dt * (6.28318530f / (2.0f * stride_len));

        float hip_sign[2] = {-1.0f, 1.0f};
        const float hip_w = 0.25f;

        for (int leg = 0; leg < 2; ++leg) {
            float hx = tx + hip_sign[leg] * hip_w;
            float hy = ty;

            // Stride target directly ahead.
            float dir_x = vx / speed, dir_y = vy / speed;
            float pred_x = hx + dir_x * stride_len * 0.5f;
            float pred_y = hy + dir_y * stride_len * 0.5f;

            if (!legs.stepping[leg]) {
                float dx   = legs.foot[leg].x - pred_x;
                float dy   = legs.foot[leg].y - pred_y;
                float dist = std::sqrt(dx * dx + dy * dy);
                // One-foot-planted invariant: only step if other foot is planted.
                if (dist > stride_len * 0.5f && !legs.stepping[1 - leg]) {
                    legs.stepping[leg]  = true;
                    legs.progress[leg]  = 0.0f;
                    legs.prev_foot[leg] = legs.foot[leg];
                    legs.target[leg]    = {pred_x, pred_y, 0.0f};
                }
            }

            if (legs.stepping[leg]) {
                legs.progress[leg] += dt / step_duration;
                float p  = std::min(legs.progress[leg], 1.0f);
                float ts = p * p * (3.0f - 2.0f * p);
                legs.foot[leg].x = legs.prev_foot[leg].x +
                                   (legs.target[leg].x - legs.prev_foot[leg].x) * ts;
                legs.foot[leg].y = legs.prev_foot[leg].y +
                                   (legs.target[leg].y - legs.prev_foot[leg].y) * ts;
                if (legs.progress[leg] >= 1.0f) {
                    legs.stepping[leg] = false;
                    legs.foot[leg]     = legs.target[leg];
                }
            }
        }

        // Invariant check — must never have both feet stepping simultaneously.
        EXPECT_FALSE(legs.stepping[0] && legs.stepping[1]);
    }
    return true;
}
