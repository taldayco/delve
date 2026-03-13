out_right = glm::cross(ref_fwd, out_up);
    float right_len = glm::length(out_right);
    if (right_len < 1e-5f) {
        // bone_dir is (anti-)parallel to ref_fwd — pick perpendicular fallback.
        glm::vec3 alt = (std::abs(out_up.z) < 0.9f)
                            ? glm::vec3(0.0f, 0.0f, 1.0f)
                            : glm::vec3(1.0f, 0.0f, 0.0f);
        out_right = glm::normalize(glm::cross(alt, out_up));
    } else {
        out_right /= right_len;
    }

    out_fwd = glm::cross(out_up, out_right);  // guaranteed orthonormal
}

struct ProceduralMesh {
    std::vector<BasaltVertex> vertices;
};

struct LegState {
    glm::vec3 foot[2]      = {};
    glm::vec3 prev_foot[2] = {};
    glm::vec3 target[2]    = {};
    float     progress[2]  = {};
    bool      stepping[2]  = {};

    // World-locked plant positions
    glm::vec3 plant_pos[2] = {};
    bool      planted[2]   = {};

    // Turn-aware step priority (-1 = none)
    int       turn_step_queued = -1;

    int       last_step_leg = -1;  // Which leg stepped most recently (-1 = none yet)
};

// All mutable per-actor animation state. Lives in ECS so it's copyable and
// accessible headlessly. Replaces the static globals in SkeletonFinaliseSystem.
struct RigState {
    // --- Velocity smoothing (SmoothDamp) ---
    glm::vec3 smooth_velocity{0.0f};   // current smoothed velocity
    glm::vec3 velocity_rate{0.0f};     // SmoothDamp internal derivative state

    // --- Leg-driven support balance ---
    float support_balance      = 0.0f;  // [-1,+1]: -1=left planted, +1=right planted
    float support_balance_rate = 0.0f;  // smooth_damp derivative

    // --- Torso lean (was s_lean_x / s_lean_y) ---
    // Smoothed lean values per spine segment (successive breaking)
    float lean_x = 0.0f;
    float lean_y = 0.0f;
    // SmoothDamp derivative state for each spine segment lean
    float chest_lean_x_rate = 0.0f;
    float chest_lean_y_rate = 0.0f;
    float neck_lean_x_rate  = 0.0f;
    float neck_lean_y_rate  = 0.0f;
    float head_lean_x_rate  = 0.0f;
    float head_lean_y_rate  = 0.0f;
    // Per-segment smoothed lean values
    float chest_lean_x = 0.0f;
    float chest_lean_y = 0.0f;
    float neck_lean_x  = 0.0f;
    float neck_lean_y  = 0.0f;
    float head_lean_x  = 0.0f;
    float head_lean_y  = 0.0f;

    // --- Arm swing (pendulum) ---
    float l_arm_target      = 0.0f;   // target swing angle for left arm
    float r_arm_target      = 0.0f;   // target swing angle for right arm
    float l_shoulder_smooth = 0.0f;   // smoothed upper-arm chain value
    float l_elbow_smooth    = 0.0f;   // smoothed lower-arm chain value (lags upper)
    float l_wrist_smooth    = 0.0f;   // smoothed hand chain value (lags lower)
    float r_shoulder_smooth = 0.0f;
    float r_elbow_smooth    = 0.0f;
    float r_wrist_smooth    = 0.0f;
    // SmoothDamp derivative states for arm joints
    float l_shoulder_rate   = 0.0f;
    float l_elbow_rate      = 0.0f;
    float l_wrist_rate      = 0.0f;
    float r_shoulder_rate   = 0.0f;
    float r_elbow_rate      = 0.0f;
    float r_wrist_rate      = 0.0f;

    // --- Idle micro-motion ---
    float breath_phase    = 0.0f;   // free-running breathing phase (rad)
    float idle_sway_phase = 0.0f;   // free-running idle lateral sway phase (rad)
    float idle_weight_phase = 0.0f; // free-running weight-shift phase (rad, ~0.3 Hz)

    // --- Grounding: foot contact quality ---
    float foot_contact_velocity[2] = {0.0f, 0.0f};

    // --- Previous velocity for acceleration computation ---
    glm::vec3 prev_velocity{0.0f};

    // --- Hip counter-animation (procedural sway fix 3) ---
    float hip_roll      = 0.0f;  // lateral hip roll (radians)
    float hip_roll_rate = 0.0f;  // smooth_damp derivative
    float hip_bob       = 0.0f;  // vertical double-bounce offset (world units)
    float hip_bob_rate  = 0.0f;  // smooth_damp derivative

    // --- Step-event hip dip ---
    float hip_dip      = 0.0f;  // vertical dip during foot transition (world units)
    float hip_dip_rate = 0.0f;  // smooth_damp derivative

    // --- Slope-driven hip tilt (Phase 1C) ---
    float hip_tilt      = 0.0f;  // lateral tilt from foot height diff (radians)
    float hip_tilt_rate = 0.0f;  // smooth_damp derivative