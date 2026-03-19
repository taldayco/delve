#pragma once
#include <flecs.h>

class InputSystem;
class CameraState;
class AnimationLogger;
class SkinnedRenderer;

void register_hybrid_systems(flecs::world &ecs,
                              InputSystem    &input,
                              CameraState    &camera,
                              AnimationLogger &anim_log,
                              SkinnedRenderer &skinned_renderer,
                              flecs::entity   player_entity);
