#pragma once
#include <flecs.h>

class InputSystem;
class SkinnedRenderer;

void register_hybrid_systems(flecs::world &ecs,
                              InputSystem    &input,
                              SkinnedRenderer &skinned_renderer,
                              flecs::entity   player_entity);
