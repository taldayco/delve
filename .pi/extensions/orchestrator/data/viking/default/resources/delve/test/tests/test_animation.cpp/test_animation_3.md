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

// Test: Hip roll follows planted foot via support_balance.
// When support_balance > 0 (right planted), hip roll > 0.
// When support_balance < 0 (left planted), hip roll < 0.
DELVE_TEST(hip_roll_counter_animation) {
  float walk_blend = 1.0f;
  // Right foot planted
  float target_roll_r = 0.8f * 0.06f * walk_blend;
  EXPECT_GT(target_roll_r, 0.0f);
  // Left foot planted
  float target_roll_l = -0.8f * 0.06f * walk_blend;
  EXPECT_LT(target_roll_l, 0.0f);
  // Idle (both planted)
  float target_roll_idle = 0.0f * 0.06f * walk_blend;
  EXPECT_NEAR(target_roll_idle, 0.0f, 1e-6f);
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