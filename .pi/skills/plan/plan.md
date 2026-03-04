---
name: plan
description: Decompose a feature request into ordered implementation subtasks
when: When starting any new feature, bug fix, or change request
---

# Planner Skill

You decompose a user's request into concrete, ordered subtasks with file paths and acceptance criteria.

## Procedure

1. **Understand the request.** Read the user's prompt carefully. Identify what's being asked for.

2. **Identify affected subsystems.** Determine which parts of the codebase are involved:
   - Terrain pipeline: `src/game/terrain/` (noise, composition, contour, generation, mesh, rendering)
   - Engine framework: `src/engine/` (app, gpu, input, camera, ui, render)
   - Shaders: `src/shaders/` (GLSL vertex/fragment/compute)
   - Actor system: `src/game/actor.h`, `src/game/render/`
   - Game state: `src/game/game_state.h`, `src/game/topo_game.cpp`
   - Config: `src/game/config.h`, `config.json`
   - Build: `CMakeLists.txt`

3. **Order by dependency.** Changes flow through the pipeline:
   - Data structures before logic that uses them
   - Noise params before noise generation
   - Mesh changes before renderer changes
   - New shaders before pipeline creation
   - C++ before CMakeLists.txt registration

4. **Output a structured plan.** For each subtask:
   ```
   ## Subtask N: [Title]
   - **Files:** [list of files to create/modify]
   - **What:** [specific changes to make]
   - **Acceptance:** [how to verify this subtask is done]
   - **Depends on:** [which prior subtasks, if any]
   ```

5. **Include test subtask.** Always end with a subtask for writing quantitative visual tests that verify the feature works.

## Rules
- Never plan changes to files you haven't read first
- Keep subtasks small — each should be one logical change
- If the request is ambiguous, state your assumptions explicitly
- Prefer modifying existing files over creating new ones
