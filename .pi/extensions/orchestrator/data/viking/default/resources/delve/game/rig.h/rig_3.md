// --- Look-at smoothing (Phase 2) ---
    float look_yaw        = 0.0f;  // current smoothed look yaw (radians)
    float look_pitch      = 0.0f;  // current smoothed look pitch (radians)
    float look_yaw_rate   = 0.0f;  // smooth_damp derivative
    float look_pitch_rate = 0.0f;  // smooth_damp derivative

    // --- Visual body orientation (decoupled from t.facing for smooth turns) ---
    float visual_facing      = 0.0f;  // smoothed body orientation (radians)
    float visual_facing_rate = 0.0f;  // smooth_damp derivative

    // --- Turn detection state ---
    float turn_delta       = 0.0f;  // signed: t.facing - visual_facing, wrapped [-PI,PI]
    float turn_magnitude   = 0.0f;  // abs(turn_delta), [0, PI]
    float turn_urgency     = 0.0f;  // turn_magnitude / PI, [0, 1]
    bool  in_large_turn    = false; // hysteresis flag (enter > 60°, exit < 20°)

    // --- Chest counter-rotation ---
    float chest_facing      = 0.0f;  // smoothed upper-body orientation (lags visual_facing)
    float chest_facing_rate = 0.0f;  // smooth_damp derivative
};

// Hip counter-animation state for rendering pass.
// Computed from gait phase; used to apply procedural hip motion to skeleton.
struct RigHipState {
    float stride_phase      = 0.0f;  // [0, 1) normalized gait phase
    float hip_rotation_deg  = 0.0f;  // lateral hip sway (degrees)
    float hip_drop_fraction = 0.0f;  // fraction of max drop [0, hip_drop_max]
    float hip_bob_y         = 0.0f;  // vertical double-bounce offset (world units)
};

// Configuration for hip counter-animation in rendering.
struct AnimationConfig {
    // Isometric height scale: squashes character for 2:1 iso proportions.
    static constexpr float ISO_CHAR_HEIGHT_SCALE = 0.816f * 0.92f;

    // Directional speed-scale coefficient: gait phase advances faster along the
    // isometric vertical axis (screen "up") to compensate for the 2:1 tile ratio.
    // Value ≈ sqrt(2)-1 ≈ 0.414 corrects the iso compression exactly.
    static constexpr float DIRECTIONAL_SPEED_SCALE = 0.41f;

    float directional_speed_scale = DIRECTIONAL_SPEED_SCALE; // dir multiplier coefficient for Y-axis iso correction
    float hip_sway_deg      = 5.0f;   // max lateral hip rotation (degrees)
    float hip_drop_max      = 0.03f;  // max fractional CoM drop during stride
    float hip_bob_amplitude = 0.02f;  // vertical double-bounce amplitude (world units)
};

// --- Phase 2: Look-at target ---
struct LookAtTarget {
    glm::vec3 position{0.0f};
    float weight = 0.0f;    // [0,1] blend toward target
    bool active = false;
};

// --- Phase 3: Arm IK goal ---
struct ArmIKGoal {
    glm::vec3 target_l{0.0f}, target_r{0.0f};
    float weight_l = 0.0f, weight_r = 0.0f;  // [0,1]: 0=pendulum FK, 1=IK
};

// --- Phase 4: Animation overlay ---
struct AnimationOverlay {
    enum class Type : uint8_t { None, Limp, Fatigue, HeavyCarry };
    Type active = Type::None;
    float intensity = 0.0f;  // [0,1]
    float phase = 0.0f;
};

// --- Phase 5: Grab state ---
struct GrabState {
    glm::vec3 grab_point{0.0f};
    float weight = 0.0f;
    bool active_l = false, active_r = false;
};

// Compute hip counter-animation state from stride phase and configuration.
// stride_phase: [0, 1) normalized gait phase
inline void compute_rig_hip_state(RigHipState &state,
                                   const AnimationConfig &cfg) {
    constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    float two_pi_phase = state.stride_phase * TWO_PI;

    state.hip_rotation_deg  = cfg.hip_sway_deg      * std::sin(two_pi_phase);
    state.hip_drop_fraction = cfg.hip_drop_max      * (1.0f - std::abs(std::cos(two_pi_phase)));
    state.hip_bob_y         = cfg.hip_bob_amplitude * std::abs(std::sin(two_pi_phase));
}