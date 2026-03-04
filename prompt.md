# Procedural Animation: Biomechanical Movement System

Implement fluid procedural animation for the player character using kinematic interpolation. Extract animation logic from `topo_game.cpp` into a dedicated module, then layer biomechanical behaviors (velocity smoothing, anticipatory lean, arm swing, idle micro-motion, grounding) on top.

## Design Constraints

- **No physics engine** — all kinematic interpolation: Lerp, SmoothDamp, sine waves
- **No new dependencies** — GLM + C++ stdlib only
- **ECS-friendly** — all mutable state lives in ECS components, zero static/global mutables
- **Headless-testable** — animation logic works without GPU context
- **Frame-rate independent** — every formula parameterized by `dt`
- **Preserve existing behavior** — extraction subtask must produce identical visual output before new behaviors are added

---

## Subtask 1 [actor]: Extract Animation Module

Move all 6 animation ECS systems from `src/game/topo_game.cpp` (lines 160-441) into a new module at `src/game/render/actor_animation.h/cpp`.

### What to extract

These systems, in order:
1. `PlayerMovementSystem` (line 160) — input → velocity → position
2. `ActorGroundingSystem` (line 192) — snap actor Z to terrain height
3. `GaitSystem` (line 207) — procedural foot placement with stride/step logic
4. `IKSystem` (line 277) — two-bone analytical IK for legs, static arm placement
5. `SkeletonFinaliseSystem` (line 379) — hip sway + torso lean
6. `AnimationLogSystem` (line 418) — telemetry logging

### New ECS component: `AnimationState`

Add to `src/game/actor.h`:

```
struct AnimationState {
    // Velocity smoothing (Subtask 2)
    glm::vec3 smooth_velocity{0};
    glm::vec3 velocity_rate{0};    // SmoothDamp internal state

    // Sway (moved from static globals in SkeletonFinaliseSystem)
    float sway_phase = 0.0f;
    float sway_amount = 0.04f;

    // Lean (moved from static globals)
    float lean_x = 0.0f;
    float lean_y = 0.0f;

    // Arm swing (Subtask 4)
    float l_arm_target = 0.0f;     // target swing angle
    float r_arm_target = 0.0f;
    float l_shoulder_smooth = 0.0f; // smoothed chain values
    float l_elbow_smooth = 0.0f;
    float l_wrist_smooth = 0.0f;
    float r_shoulder_smooth = 0.0f;
    float r_elbow_smooth = 0.0f;
    float r_wrist_smooth = 0.0f;

    // Idle (Subtask 5)
    float breath_phase = 0.0f;
    float idle_sway_phase = 0.0f;

    // Grounding (Subtask 6)
    float foot_contact_velocity[2] = {};
};
```

### Module interface: `actor_animation.h`

```
#pragma once
#include <flecs.h>

class InputSystem;
class CameraState;
class AnimationLogger;

void register_animation_systems(flecs::world &ecs,
                                 InputSystem &input,
                                 CameraState &camera,
                                 AnimationLogger &anim_log,
                                 flecs::entity player_entity);
```

### Changes to `topo_game.cpp`

Replace lines 160-441 with a single call:

```
register_animation_systems(ecs, input, camera, anim_log, player_entity);
```

Add `#include "render/actor_animation.h"` to the includes.

### CMakeLists.txt

Add `src/game/render/actor_animation.cpp` after `src/game/render/actor_renderer.cpp` in the `topogen` target (line 168).

### Acceptance criteria

- Project builds: `cmake --build build -j$(nproc)`
- Visual behavior is byte-identical (same systems, same order, same math)
- The 4 static globals (`s_sway_phase`, `s_sway_amt`, `s_lean_x`, `s_lean_y`) from `topo_game.cpp:374-377` are replaced by `AnimationState` component fields
- `AnimationState` is added to the player entity alongside the existing components

---

## Subtask 2 [actor]: Impulse — Velocity Smoothing

Replace the instant velocity assignment in `PlayerMovementSystem` with a critically-damped spring (SmoothDamp) so the character ramps up/down over ~0.1 seconds, creating a feeling of mass.

### Current code (lines 176-184)

```cpp
vel->x = 0.0f;
vel->y = 0.0f;
if (in.held[(int)Action::MoveUp])    vel->y -= gait->move_speed;
// ... etc
t->x += vel->x * dt;
t->y += vel->y * dt;
```

### SmoothDamp formula (Unity-style critically damped spring)

```
smooth_damp(current, target, &velocity_rate, smooth_time, dt):
    omega = 2.0 / smooth_time
    x = omega * dt
    exp_factor = 1.0 / (1.0 + x + 0.48*x*x + 0.235*x*x*x)
    delta = current - target
    temp = (velocity_rate + omega * delta) * dt
    velocity_rate = (velocity_rate - omega * temp) * exp_factor
    return target + (delta + temp) * exp_factor
```

### New logic

```
// Compute raw desired velocity from input (same as before)
desired_x, desired_y = input_to_velocity(...)

// Smooth each axis independently
anim.smooth_velocity.x = smooth_damp(anim.smooth_velocity.x, desired_x,
                                      &anim.velocity_rate.x, 0.1f, dt)
anim.smooth_velocity.y = smooth_damp(anim.smooth_velocity.y, desired_y,
                                      &anim.velocity_rate.y, 0.1f, dt)

// Use smoothed velocity for movement
vel->x = anim.smooth_velocity.x
vel->y = anim.smooth_velocity.y
t->x += vel->x * dt
t->y += vel->y * dt
```

The `smooth_damp` function should be a free function in `actor_animation.cpp` (or a static helper). It operates per-component (float in, float out).

### Acceptance criteria

- From standstill, character reaches 90% of `move_speed` within 0.2s (2x smooth_time)
- After releasing input, character decelerates to <5% within 0.3s
- No overshoot (critically damped, not underdamped)
- Frame-rate independent: same convergence at 30 FPS and 120 FPS

---

## Subtask 3 [actor]: Anticipation — CoM Shift + Torso Lean

Replace the existing simple lean (lines 402-413) with acceleration-driven lean that propagates through the spine chain with successive breaking (each segment lags its parent).

### Current code (SkeletonFinaliseSystem)

```cpp
float lean = 0.03f;
s_lean_x = 0.0f; s_lean_y = 0.0f;
if (speed > 0.001f) {
    s_lean_x = vel.x / speed * lean * speed;
    s_lean_y = vel.y / speed * lean * speed;
    glm::vec3 lean_vec(s_lean_x, s_lean_y, 0.0f);
    pose.joints[(int)J::CHEST] += lean_vec;
    pose.joints[(int)J::NECK]  += lean_vec;
    pose.joints[(int)J::HEAD]  += lean_vec;
}
```

### New lean model

Lean angle is proportional to **acceleration** (change in velocity), not just velocity. This creates the anticipation effect — the character leans into movement direction when starting, and leans back when stopping.

```
// Compute acceleration from velocity delta
acceleration = (current_velocity - previous_velocity) / dt

// Lean magnitude: map acceleration to angle (5-10 degrees max)
max_lean_angle = radians(8.0)
lean_factor = clamp(length(acceleration) / move_speed * 0.1, 0, max_lean_angle)
lean_direction = normalize(acceleration)  // lean INTO acceleration

// Smooth the lean (don't snap)
anim.lean_x = smooth_damp(anim.lean_x, lean_direction.x * lean_factor, ...)
anim.lean_y = smooth_damp(anim.lean_y, lean_direction.y * lean_factor, ...)
```

### Successive breaking through spine chain

Each joint gets a fraction of the lean, with increasing lag:

```
// Chest gets full lean, smoothed fast (0.05s)
chest_lean = smooth(full_lean, 0.05s)

// Neck gets 70% of chest lean, smoothed slower (0.08s)
neck_lean = smooth(chest_lean * 0.7, 0.08s)

// Head gets 50% of chest lean, smoothed slowest (0.12s)
head_lean = smooth(chest_lean * 0.5, 0.12s)
```

Apply as offsets to `CHEST`, `NECK`, `HEAD` joints (same as current code, but with the cascaded values instead of uniform).

### Keep existing hip sway

The `s_sway_phase`/`s_sway_amt` logic stays functionally the same but reads/writes from `AnimationState` instead of statics.

### Acceptance criteria

- When starting to move, chest tilts forward 5-10 degrees within 0.1s
- When stopping, chest tilts backward momentarily (deceleration lean)
- Head visibly lags behind chest during direction changes
- At constant velocity, lean settles near zero (acceleration is zero)

---

## Subtask 4 [actor]: Arm Swing with Joint Delay Chain

Replace the current static arm placement (IKSystem lines 317-321) with pendulum-style arm swing that opposes the legs, with successive breaking delays through the arm chain.

### Current code (arms hang rigidly)

```cpp
pose.joints[(int)J::L_ELBOW] = l_shoulder + glm::vec3(0, 0, -cfg.arm_len * 0.8f);
pose.joints[(int)J::L_WRIST] = pose.joints[(int)J::L_ELBOW] + glm::vec3(0, 0, -cfg.forearm_len * 0.8f);
// same for right
```

### Pendulum swing model

Arm swing opposes the ipsilateral leg: when the left leg is forward, the left arm swings backward. This is driven by the gait phase.

```
// Swing angle from gait phase (opposing legs)
// Left arm opposes left leg: offset by pi
l_arm_target = sin(gait.phase + PI) * swing_amplitude
r_arm_target = sin(gait.phase)      * swing_amplitude

// swing_amplitude scales with speed (no swing at idle)
swing_amplitude = clamp(speed / move_speed, 0, 1) * radians(30)
```

### Successive breaking (joint delay chain)

Each joint in the arm chain smooths toward its target with increasing lag:

```
// Shoulder: fast response (0.02s smooth_time)
anim.l_shoulder_smooth = smooth_damp(anim.l_shoulder_smooth, l_arm_target, 0.02)

// Elbow: medium lag (0.05s smooth_time)
anim.l_elbow_smooth = smooth_damp(anim.l_elbow_smooth, anim.l_shoulder_smooth, 0.05)

// Wrist: most lag (0.08s smooth_time)
anim.l_wrist_smooth = smooth_damp(anim.l_wrist_smooth, anim.l_elbow_smooth, 0.08)
```

### Applying swing to joint positions

The swing angle rotates each joint around the shoulder in the forward-axis plane:

```
// For each arm:
fwd = vec3(cos(facing), sin(facing), 0)
down = vec3(0, 0, -1)

// Shoulder swing: rotate the "hang down" vector by shoulder_smooth angle around right axis
shoulder_dir = rotate(down, shoulder_smooth, right_axis)
elbow_pos = shoulder + shoulder_dir * arm_len

// Elbow: natural 25-degree rest bend + swing modulation
elbow_bend = radians(25) + elbow_smooth * 0.3
elbow_dir = rotate(shoulder_dir, elbow_bend, right_axis)
wrist_pos = elbow_pos + elbow_dir * forearm_len
```

### Centripetal turn reaction

During sharp turns (high angular velocity of facing), arms swing outward:

```
angular_velocity = (facing - prev_facing) / dt
centripetal_offset = rght * angular_velocity * 0.05  // subtle outward push
// Add to elbow and wrist positions
```

### Acceptance criteria

- Left arm and left leg are in anti-phase (when leg forward, arm backward)
- Wrist visibly lags behind shoulder during direction changes
- At rest (speed < 0.1), arms hang naturally with ~25 degree elbow bend
- Arm swing amplitude scales linearly with speed, zero when idle

---

## Subtask 5 [actor]: Idle Micro-Motion

Add subtle breathing and weight-shift animations when the character is standing still (speed < 0.1).

### Breathing

A sine wave on the chest Z position simulating rib cage expansion:

```
if (speed < 0.1) {
    anim.breath_phase += dt * 2.0 * PI * 0.6;  // ~0.6 Hz breathing rate
    float breath_offset = sin(anim.breath_phase) * 0.008;  // 8mm amplitude
    pose.joints[CHEST].z += breath_offset;
    pose.joints[NECK].z  += breath_offset * 0.6;  // neck follows partially
    pose.joints[HEAD].z  += breath_offset * 0.4;
}
```

### Weight shift sway

A very slow lateral sway simulating natural postural adjustment:

```
if (speed < 0.1) {
    anim.idle_sway_phase += dt * 2.0 * PI * 0.15;  // ~0.15 Hz, very slow
    float idle_sway = sin(anim.idle_sway_phase) * 0.01;  // 1cm lateral
    vec3 sway_vec = rght * idle_sway;
    pose.joints[ROOT]  += sway_vec;
    pose.joints[SPINE] += sway_vec;
}
```

### Transition

When speed crosses above 0.1, these effects should fade out smoothly (multiply by a `1 - clamp(speed / 0.2, 0, 1)` factor) rather than popping off.

### Acceptance criteria

- Chest Z oscillates at ~0.6 Hz with ~8mm amplitude when idle
- Idle sway is perceptible but subtle (< 1.5cm total lateral displacement)
- Both effects fade smoothly when movement begins (no pop)
- Breathing phase resets to 0 would cause a pop — do NOT reset, just let it free-run

---

## Subtask 6 [actor]: Grounding — Foot Skating Prevention

Improve the existing gait system to enforce a one-foot-planted invariant and scale step duration with speed for natural cadence.

### One-foot-planted invariant

Currently both legs can step simultaneously (no guard). Add a constraint:

```
// In GaitSystem, before triggering a new step:
if (legs.stepping[other_leg]) {
    // Other leg is mid-step — don't start a new step
    continue;
}
```

This ensures at least one foot is always planted, preventing the "floating" look.

### Speed-adaptive step duration

Currently `step_duration` is a fixed 0.25s. Scale it with speed for natural cadence:

```
// Faster movement = shorter step duration (quicker steps)
// Slower movement = longer, more deliberate steps
float speed_ratio = clamp(speed / gait.move_speed, 0.2, 1.0);
float adaptive_duration = gait.step_duration / speed_ratio;
// Use adaptive_duration instead of gait.step_duration in progress calculation
legs.progress[leg] += dt / adaptive_duration;
```

### Foot contact velocity logging

Track the foot's velocity at the moment it plants (progress reaches 1.0), for quality metrics:

```
if (legs.progress[leg] >= 1.0f) {
    // Compute foot velocity at contact
    float contact_vel = length(legs.foot[leg] - legs.prev_foot[leg]) / gait.step_duration;
    anim.foot_contact_velocity[leg] = contact_vel;

    legs.stepping[leg] = false;
    legs.foot[leg] = legs.target[leg];
}
```

Ideal foot contact velocity is near zero (foot decelerates to a stop). Values above a threshold indicate skating.

### Acceptance criteria

- `legs.stepping[0] && legs.stepping[1]` is never true simultaneously
- At slow speeds, steps take longer; at high speeds, steps are quicker
- Foot contact velocity is logged in `AnimationState` for metric extraction
- No visual regression in foot placement accuracy

---

## Subtask 7 [actor]: Enhanced AnimationLogger

Extend `src/game/animation_log.h` with 3 new logging methods that capture the new animation behaviors for telemetry and debugging.

### `log_dynamics`

Captures velocity smoothing and lean data:

```cpp
void log_dynamics(const AnimationState &anim, const Velocity &vel, float dt) {
    if (!active || !file) return;
    // Compute jerk (rate of change of acceleration)
    vec3 accel = (vec3(vel.x, vel.y, 0) - prev_velocity) / dt;
    float jerk = length(accel - prev_accel) / dt;
    prev_accel = accel;
    prev_velocity = vec3(vel.x, vel.y, 0);

    // CoM offset from smooth velocity
    float com_offset = length(vec3(anim.smooth_velocity) - vec3(vel.x, vel.y, 0));

    fprintf(file,
        ",\"dynamics\":{\"jerk\":%.4f,\"com_offset\":%.4f,"
        "\"lean_x\":%.4f,\"lean_y\":%.4f,"
        "\"smooth_vel\":[%.4f,%.4f]}",
        jerk, com_offset,
        anim.lean_x, anim.lean_y,
        anim.smooth_velocity.x, anim.smooth_velocity.y);
}
```

This requires adding `prev_velocity` and `prev_accel` as private members of `AnimationLogger`.

### `log_arm_swing`

Captures arm swing angles and lag:

```cpp
void log_arm_swing(const AnimationState &anim) {
    if (!active || !file) return;
    // Lag = difference between target and smoothed value
    float l_shoulder_lag = abs(anim.l_arm_target - anim.l_shoulder_smooth);
    float l_elbow_lag    = abs(anim.l_shoulder_smooth - anim.l_elbow_smooth);
    float l_wrist_lag    = abs(anim.l_elbow_smooth - anim.l_wrist_smooth);

    fprintf(file,
        ",\"arm_swing\":{"
        "\"l_target\":%.4f,\"r_target\":%.4f,"
        "\"l_shoulder_lag\":%.4f,\"l_elbow_lag\":%.4f,\"l_wrist_lag\":%.4f,"
        "\"r_shoulder_lag\":%.4f,\"r_elbow_lag\":%.4f,\"r_wrist_lag\":%.4f}",
        anim.l_arm_target, anim.r_arm_target,
        l_shoulder_lag, l_elbow_lag, l_wrist_lag,
        abs(anim.r_arm_target - anim.r_shoulder_smooth),
        abs(anim.r_shoulder_smooth - anim.r_elbow_smooth),
        abs(anim.r_elbow_smooth - anim.r_wrist_smooth));
}
```

### `log_grounding`

Captures foot contact quality:

```cpp
void log_grounding(const AnimationState &anim, const LegState &legs) {
    if (!active || !file) return;
    fprintf(file,
        ",\"grounding\":{"
        "\"both_stepping\":%s,"
        "\"l_contact_vel\":%.4f,\"r_contact_vel\":%.4f,"
        "\"l_stepping\":%s,\"r_stepping\":%s}",
        (legs.stepping[0] && legs.stepping[1]) ? "true" : "false",
        anim.foot_contact_velocity[0], anim.foot_contact_velocity[1],
        legs.stepping[0] ? "true" : "false",
        legs.stepping[1] ? "true" : "false");
}
```

### Integration

Call these 3 new methods from the `AnimationLogSystem` after the existing `log_finalize` call, passing the `AnimationState` component.

### Acceptance criteria

- All 3 methods emit valid JSON fragments (test by toggling logging with existing keybind)
- `log_dynamics` jerk value is finite and non-negative
- `log_grounding.both_stepping` is always `false` (validates Subtask 6)
- No crash when logging is inactive (early-return guards)

---

## Subtask 8 [engine]: New Metrics + Tests

Extend the test infrastructure with 4 new metric functions and 6 new test cases validating the animation behaviors.

### New metrics in `src/test/animation_metrics.h`

```cpp
// Returns value in [0, 1]. 1.0 = perfect opposition (left arm vs left leg anti-phase)
inline float arm_phase_opposition(float l_arm_angle, float l_leg_forward) {
    // Both should have opposite signs when in opposition
    float product = l_arm_angle * l_leg_forward;
    return product < 0 ? 1.0f : 0.0f;  // negative product = anti-phase
}

// Returns ratio of wrist_lag / shoulder_lag. Should be > 1.0 (wrist lags more)
inline float joint_lag_ratio(float shoulder_lag, float wrist_lag) {
    if (shoulder_lag < 1e-6f) return 1.0f;
    return wrist_lag / shoulder_lag;
}

// Returns foot velocity at ground contact. Lower = better (0 = perfect plant)
inline float foot_contact_velocity(const AnimationState &anim, int leg) {
    return anim.foot_contact_velocity[leg];
}

// Returns correlation between lean magnitude and acceleration magnitude
// High correlation (> 0.7) means lean tracks acceleration well
inline float lean_acceleration_correlation(float lean_mag, float accel_mag,
                                            float max_lean) {
    float expected = clamp(accel_mag * 0.01f, 0.0f, max_lean);
    float error = abs(lean_mag - expected);
    return 1.0f - clamp(error / max_lean, 0.0f, 1.0f);
}
```

### New tests in `src/test/tests/test_animation.cpp`

**Test 1: `smooth_damp_convergence`**
— Call `smooth_damp(0, 1, &rate, 0.1, dt)` in a loop for 1 second. Assert final value > 0.99.

**Test 2: `arm_phase_opposes_leg`**
— Set gait phase to several values, compute arm target angles. Assert left arm and left leg forward projections have `arm_phase_opposition > 0.8` averaged over a full cycle.

**Test 3: `joint_delay_ordering`**
— Apply a step input to arm target. Advance 3 frames. Assert `|shoulder_smooth - target| < |elbow_smooth - target| < |wrist_smooth - target|` (shoulder converges fastest).

**Test 4: `idle_breathing_frequency`**
— Run idle micro-motion for 5 seconds, count zero-crossings of chest Z offset. Assert frequency is 0.5-0.8 Hz.

**Test 5: `velocity_smoothing_has_weight`**
— Set desired velocity to `move_speed`, advance 1 frame at dt=1/60. Assert actual velocity < 50% of desired (hasn't reached full speed yet due to smoothing).

**Test 6: `no_simultaneous_stepping`**
— Simulate gait for 200 frames with random velocity inputs. Assert `legs.stepping[0] && legs.stepping[1]` is never true.

### CMakeLists.txt

No changes needed for tests — `test_animation.cpp` is already in the `delve_tests` sources. The new metrics are header-only. However, if any test needs `AnimationState`, ensure `actor.h` includes are accessible (already included via existing include paths).

### Acceptance criteria

- `cmake --build build --target delve_tests -j$(nproc)` builds
- `./build/delve_tests` passes all 6 new tests plus the 5 existing ones
- Each test is independent (no shared mutable state between tests)
- Tests run headless (no GPU, no SDL initialization)
