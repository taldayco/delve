#pragma once
#include <flecs.h>

class InputSystem;
class CameraState;
class AnimationLogger;

// Register all 10 procedural animation ECS systems into the world.
// Systems run in PostUpdate phase, in this order:
//   1. PlayerMovementSystem   — input → SmoothDamp velocity → position + look-at target
//   2. ActorGroundingSystem   — snap actor Z to terrain height (foot-average)
//   3. GaitSystem             — procedural foot placement + continuous Z tracking
//   3.5 GrabDriveSystem       — GrabState → ArmIKGoal + weight compensation
//   4. IKSystem               — two-bone leg IK + pendulum arm swing + arm IK blend
//   5. OverlaySystem          — additive animation overlays (limp, fatigue, heavy carry)
//   6. LookAtSystem           — procedural head/neck/chest tracking
//   7. SkeletonFinaliseSystem — hip sway/tilt, spine lean, idle micro-motion, derived joints
//   7.5 RigTransformSystem     — per-joint world-space mat4 from finalized positions
//   8. AnimationLogSystem     — JSONL telemetry via AnimationLogger
void register_rig_systems(flecs::world &ecs,
                           InputSystem    &input,
                           CameraState    &camera,
                           AnimationLogger &anim_log,
                           flecs::entity   player_entity);
