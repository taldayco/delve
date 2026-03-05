#pragma once
#include <flecs.h>

class InputSystem;
class CameraState;
class AnimationLogger;

// Register all 6 procedural animation ECS systems into the world.
// Systems run in PostUpdate phase, in this order:
//   1. PlayerMovementSystem  — input → SmoothDamp velocity → position
//   2. ActorGroundingSystem  — snap actor Z to terrain height (adaptive spring)
//   3. GaitSystem            — procedural foot placement (one-foot-planted)
//   4. IKSystem              — two-bone leg IK + pendulum arm swing
//   5. SkeletonFinaliseSystem — hip sway, spine lean, idle micro-motion, derived joints
//   6. AnimationLogSystem    — JSONL telemetry via AnimationLogger
void register_rig_systems(flecs::world &ecs,
                           InputSystem    &input,
                           CameraState    &camera,
                           AnimationLogger &anim_log,
                           flecs::entity   player_entity);
