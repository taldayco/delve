#pragma once
#include "../actor.h"
#include "../animation_log.h"
#include "camera/camera.h"
#include <flecs.h>

class InputSystem;

// Register all 6 animation ECS systems.
// Systems capture player_entity, input, camera, and anim_log by reference.
// MapData is read via ecs.get<MapData>() inside each system.
void register_animation_systems(flecs::world& ecs,
                                flecs::entity& player_entity,
                                InputSystem& input,
                                CameraState& camera,
                                AnimationLogger& anim_log);
