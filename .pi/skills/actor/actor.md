---
name: actor
description: Implement changes to actor/animation system
when: When modifying skeletal animation, procedural gait, player character, or actor rendering
---

# Actor Specialist Skill

You implement changes to the actor system: skeletal poses, procedural gait, player character, and actor rendering.

## Joint System (17 joints)
Defined in `Joint` enum (`actor.h`):
```
ROOT → SPINE → CHEST → NECK → HEAD
                    ├→ L_SHOULDER → L_ELBOW → L_WRIST
                    ├→ R_SHOULDER → R_ELBOW → R_WRIST
         ├→ L_HIP → L_KNEE → L_ANKLE
         └→ R_HIP → R_KNEE → R_ANKLE
```

`SkeletonPose`: array of `glm::vec3` positions indexed by `(int)Joint::XXX`

## Body Configuration (ActorConfig)
All dimensions in world units:
- `hip_width`, `shoulder_width` — horizontal span
- `leg_len`, `shin_len` — upper/lower leg
- `torso_len`, `neck_len` — trunk
- `head_radius` — sphere
- `arm_len`, `forearm_len` — upper/lower arm
- `limb_radius`, `torso_radius` — cylinder radii

## Procedural Gait (ProceduralGait)
- `phase` — current walk cycle phase
- `stride_len` (0.6) — distance per step in world units
- `step_height` (0.18) — foot lift height
- `step_duration` (0.25) — seconds per step
- `move_speed` (4.0) — world units per second

## Leg State (LegState)
Parallel arrays indexed [0]=left, [1]=right:
- `foot[2]` — current foot position
- `prev_foot[2]` — previous foot position
- `target[2]` — target foot placement
- `progress[2]` — interpolation factor [0,1]
- `stepping[2]` — is this foot currently stepping?

## ECS Components
- `Player` — empty tag identifying the player entity
- `ActorTag` — empty tag for all actors
- `Transform` — position (x, y, z) + facing (yaw in radians, 0 = +X)
- `Velocity` — linear velocity (x, y, z)

## Actor Rendering
- Handled by `ActorRenderer` in `src/game/render/actor_renderer.cpp`
- Integrates with terrain depth for correct occlusion
- Joint positions computed from ActorConfig + ProceduralGait each frame

## Conventions
- Facing angle always in radians (0 = +X direction)
- All body dimensions relative to world units (HEX_SIZE = 8.0)
- Left/Right prefixed with L_/R_ in Joint enum
- Gait parameters designed for humanoid biped — adjust for different body types
