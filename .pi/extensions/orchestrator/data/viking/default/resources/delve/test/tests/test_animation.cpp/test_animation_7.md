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
  float visual_facing = 0.0f;
  float visual_facing_rate = 0.0f;

  // 60 frames forward, then 120 frames reversed.
  for (int frame = 0; frame < 180; ++frame) {
    if (frame == 60) vel_x = -4.0f;
    pos_x += vel_x * dt;
    float speed = fabsf(vel_x);
    float vel_dx = vel_x / speed;
    float facing = (vel_x > 0) ? 0.0f : 3.14159265f;

    // Smooth visual_facing like the live system
    {
      float delta = facing - visual_facing;
      while (delta >  glm::pi<float>()) delta -= glm::two_pi<float>();
      while (delta < -glm::pi<float>()) delta += glm::two_pi<float>();
      visual_facing = smooth_damp_test(visual_facing, visual_facing + delta,
                                        &visual_facing_rate, 0.15f, dt);
    }

    float vf_rght_x = -sinf(visual_facing);
    float hip_sign[2] = {-1.0f, 1.0f};

    float speed_ratio = std::max(0.4f, std::min(1.0f, speed / gait.move_speed));
    float adaptive_duration = gait.step_duration / speed_ratio;

    for (int leg = 0; leg < 2; ++leg) {
      int other = 1 - leg;
      float hip_x = pos_x + vf_rght_x * hip_sign[leg] * cfg.hip_width;
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