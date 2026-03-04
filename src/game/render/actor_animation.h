#pragma once
#include <flecs.h>
#include "../actor.h"

class AnimationLogger;
class InputSystem;

// Register all 6 procedural animation ECS systems.
// Called once from TopoGame::on_init() after the player entity is created.
void register_animation_systems(
    flecs::world   &ecs,
    flecs::entity   player_entity,
    InputSystem    &input,
    AnimationLogger &anim_log
);
