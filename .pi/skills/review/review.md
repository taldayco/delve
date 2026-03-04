---
name: review
description: Review code changes before PR creation
when: After implementation and testing, before creating a pull request
---

# Code Reviewer Skill

You review code changes for quality, safety, correctness, and consistency before they become a PR.

## Review Checklist

### 1. Compilation
- Does `cmake --build build -j$(nproc)` succeed with no warnings?
- If new source files were added, are they registered in CMakeLists.txt?
- If new shaders were added, are they in GRAPHICS_SHADER_SOURCES or COMPUTE_SHADER_SOURCES?

### 2. Correctness
- Do the changes actually implement what was requested?
- Are hex coordinate invariants maintained? (q,r axial, vectors sized width*height)
- Are terrain_map values using the correct constants (TERRAIN_EMPTY, TERRAIN_BASALT, etc.)?
- Are noise parameters within reasonable ranges?
- Is the pipeline order respected? (noise → composition → contour → generation → mesh → rendering)

### 3. Memory & Resource Safety
- No raw new/delete without matching cleanup
- GPU resources created in init() and released in cleanup()
- UploadManager reset() called per frame, alloc() checked for overflow
- No dangling pointers to temporary vectors
- ECS components are copyable (no mutex/atomic fields)

### 4. Performance
- No O(N^2) or worse algorithms on 1024x1024 pixel data
- Async terrain generation goes through TaskSystem, not main thread
- No unnecessary copies of large data (MapData, TerrainMesh)
- Shader uniforms properly padded for std140/std430

### 5. Consistency
- New code matches conventions in the same directory
- Naming: axial coords (q,r), pixel coords (x,y), facing in radians
- Headers use #pragma once
- No unnecessary refactoring of existing code
- No added comments, docstrings, or type annotations to unchanged code

### 6. Tests
- Are there tests for the new/changed behavior?
- Do tests use deterministic seeds?
- Do tests use smaller map sizes (256x256) for speed?
- Do all tests pass? (`./build/delve_tests`)

## Review Output Format
```
## Review: [APPROVE / REQUEST CHANGES]

### Summary
[1-2 sentence summary of what changed]

### Issues (if any)
1. [file:line] [severity: critical/warning/nit] — [description]

### Tests
- [PASS/FAIL] All tests pass
- [YES/NO] New behavior is tested
```
