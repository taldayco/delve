// Apply planted-foot clamp (mirrors live GaitSystem).
    for (int leg = 0; leg < 2; ++leg) {
      float hip_x = pos_x + vf_rght_x * hip_sign[leg] * cfg.hip_width;
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
      float hip_x = pos_x + vf_rght_x * hip_sign[leg] * cfg.hip_width;
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

// ---- Rig shape / rendering geometry tests ----

// Test: Structural joints (ROOT..R_TOE = 0..23) come before IK/virtual joints
// (POLE_KNEE_L..IK_HAND_R = 24..31). Verifies enum layout assumed by the renderer
// joint-sphere loop.
DELVE_TEST(rig_structural_joints_before_ik) {
    // R_TOE must be the last structural joint (index 23).
    EXPECT_EQ((int)Joint::R_TOE, 23);
    // IK joints start immediately after.
    EXPECT_EQ((int)Joint::POLE_KNEE_L, 24);
    EXPECT_LT((int)Joint::R_TOE, (int)Joint::POLE_KNEE_L);
    // Full range: 8 IK joints (24–31), COUNT = 32.
    EXPECT_EQ((int)Joint::IK_HAND_R, 31);
    EXPECT_EQ((int)Joint::COUNT, 32);
    return true;
}

// Test: Bone octahedron equatorial ring is non-degenerate for all bone widths
// in the default ActorConfig. The equator sits at 20% of bone length; that
// distance and the width must both be strictly positive.
DELVE_TEST(rig_bone_oct_equator_nonzero) {
    ActorConfig cfg;
    constexpr float EQUATOR_T = 0.2f;