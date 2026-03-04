#pragma once
#include "actor.h"
#include "animation_log.h"
#include "input/input.h"
#include "camera/camera.h"
#include <flecs.h>

// Register all 6 animation ECS systems into the world.
// Systems run in PostUpdate phase after input is processed.
// player_entity must have: Transform, Velocity, AnimationState, ActorConfig,
//                          ProceduralGait, LegState, SkeletonPose components.
void register_animation_systems(
    flecs::world &ecs,
    flecs::entity player_entity,
    InputSystem &input,
    AnimationLogger &anim_log);
