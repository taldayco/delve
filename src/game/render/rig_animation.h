#pragma once
#include <flecs.h>

class InputSystem;
class CameraState;
class AnimationLogger;

void register_rig_systems(flecs::world &ecs,
                           InputSystem    &input,
                           CameraState    &camera,
                           AnimationLogger &anim_log,
                           flecs::entity   player_entity);
