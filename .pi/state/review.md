## Review: REQUEST_CHANGES

### Summary
This diff does not implement the requested feature (bipedal humanoid movement and proportion fixes for the player character). The changes only modify the ImGui submodule reference and add a test executable target to CMakeLists.txt — neither of which addresses arm proportions, locomotion, or skeletal animation.

### Issues

1. **[CMakeLists.txt] [critical]** — The ImGui directory path was changed from `third_party/imgui` to `imgui`. This is an unrelated, unexplained path change that could break builds depending on where the submodule is actually checked out. Needs justification or revert.

2. **[imgui submodule] [warning]** — The ImGui submodule was bumped to a new commit (`ba84d2d`). This is unrelated to the task and should not be included unless there is a specific dependency reason. Unexplained dependency updates are a liability.

3. **[overall] [critical]** — The actual feature — bipedal locomotion system and corrected humanoid proportions — is entirely absent. No changes to:
   - Skeletal rig proportions (arm/torso/leg length ratios)
   - Animation state machine (walk cycle, idle, run)
   - Inverse kinematics or procedural foot placement
   - Any actor/animation source files in `src/game/` or `src/engine/`

4. **[CMakeLists.txt:delve_tests] [warning]** — The new `delve_tests` target includes `src/test/tests/test_animation.cpp`, which should cover animation behavior. If this file exists and tests pass, confirm the tests actually assert bipedal motion properties (joint angle ranges, stride symmetry, limb length ratios) rather than being empty stubs.

5. **[CMakeLists.txt:delve_tests] [nit]** — The test target links against `SDL3::SDL3` but animation and noise unit tests should not require a GPU/windowing dependency. Consider whether this link is actually needed.

### Tests
- **PASS** All tests pass
- **NO** New behavior (bipedal locomotion, proportion correction) is not tested — no relevant implementation exists to test