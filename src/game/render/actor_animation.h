#pragma once
#include <flecs.h>

class InputSystem;
class AnimationLogger;

// Register all 6 animation ECS systems into the world.
// - player_entity: entity with Player, Transform, Velocity, ActorConfig,
//                  ProceduralGait, LegState, SkeletonPose, AnimationState
// - input:    PlayerMovementSystem reads from this
// - anim_log: AnimationLogSystem writes to this
void register_animation_systems(
    flecs::world     &ecs,
    flecs::entity     player_entity,
    InputSystem      &input,
    AnimationLogger  &anim_log);
