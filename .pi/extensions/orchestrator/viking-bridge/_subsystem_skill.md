# Delve Subsystem Taxonomy

Canonical subsystems: terrain, actor, shader, engine

## Keyword → Subsystem Mapping
- player/character/humanoid/npc/creature/figure → actor
- walk/run/move/locomotion/gait/stride/step → actor + animation
- limb/joint/arm/leg/torso/body/head/spine/shoulder/hip/knee/elbow → actor
- animation/animate/skeleton/bone/keyframe/rig/pose/ik → animation (→ actor)
- render/draw → actor (render subsystem)
- terrain/noise/palette/hex/contour/lava/mesh/elevation/plateau → terrain
- engine/camera/input/gpu/shader/test → engine (or shader for shader keyword)

## Directory Layout
- src/game/terrain/ — Terrain generation pipeline
- src/game/render/ — Actor/rig rendering
- src/shaders/ — GLSL shaders (SPIR-V compiled)
- src/engine/ — Reusable engine framework
- src/test/ — Test infrastructure
- src/game/ — Top-level game files (topo_game, config, joint_mapping)
