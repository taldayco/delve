---
globs: src/game/render/**,src/game
---

# Actor Module Rules

## Skeletal System
- 17 joints defined in `Joint` enum: ROOT, SPINE, CHEST, NECK, HEAD, L/R_SHOULDER, L/R_ELBOW, L/R_WRIST, L/R_HIP, L/R_KNEE, L/R_ANKLE
- `SkeletonPose`: array of `glm::vec3` positions indexed by `Joint` enum
- Left/Right naming convention: `L_` and `R_` prefix (e.g., `L_SHOULDER`, `R_KNEE`)

## Actor Components (ECS)
- `Player`: empty tag struct (identifies player entity)
- `ActorTag`: empty tag struct
- `Transform`: x, y, z position + facing (yaw in radians, 0 = +X direction)
- `Velocity`: x, y, z linear velocity
- `ActorConfig`: body dimensions — hip_width, shoulder_width, leg_len, shin_len, torso_len, neck_len, head_radius, arm_len, forearm_len, limb_radius, torso_radius
- `ProceduralGait`: phase, stride_len(0.6), step_height(0.18), step_duration(0.25), move_speed(4.0)
- `LegState`: parallel arrays[2] for left/right foot tracking (foot, prev_foot, target, progress, stepping)

## Conventions
- Facing angle is in radians (0 = +X direction)
- Leg state uses parallel arrays indexed [0]=left, [1]=right
- Actor rendering integrates with terrain depth
- All body dimensions in world units relative to HEX_SIZE (8.0)
