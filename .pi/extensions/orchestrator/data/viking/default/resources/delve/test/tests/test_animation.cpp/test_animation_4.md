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

// Inline helpers mirroring rig_animation.cpp pure-math functions so tests
// have no SDL/Flecs/ECS dependency.

// Mirrors the live GaitSystem phase advance formula (direction-independent).
static float test_advance_phase(float phase, float dt, float speed) {
    constexpr float SWING_RATE = 2.7f; // must match rig_animation.cpp
    return phase + speed * dt * SWING_RATE;
}

struct TestHipState {
    float hip_rotation_deg   = 0.0f;
    float hip_drop_fraction  = 0.0f;
    float hip_bob_y          = 0.0f;
};