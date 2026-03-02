---
name: engine
description: Implement changes to the core engine framework
when: When modifying application lifecycle, GPU context, input handling, camera, UI integration, or rendering infrastructure
---

# Engine Specialist Skill

You implement changes to the reusable engine framework in `src/engine/`.

## Module Map

### Application (`app.h/cpp`)
- `Application` base class with virtual lifecycle hooks
- `run()` → main loop: poll events → fixed timestep ECS progress → render tool window → render game window
- Fixed timestep: 0.0167s, max frame clamp 0.25s
- Override: `on_init`, `on_event`, `on_render_tool`, `on_render_game`, `on_cleanup`
- Optional: `on_pre_frame_game`, `wants_game_window_open/close`

### GPU (`gpu/gpu.h/cpp`)
- `GpuContext`: SDL_GPUDevice, windows, UploadManager
- `FrameContext`: per-frame command buffer, swapchain, render pass
- Dual-window: tool window (always) + game window (on demand)
- `UploadManager`: persistent mapped buffer, `reset()` per frame, `alloc()` for suballocations
- Helper functions: `gpu_upload_buffer()`, `upload_pixels_to_texture()`, `gpu_create_buffer()`

### Input (`input/input.h/cpp`)
- `Action` enum with keyboard bindings via `bind(scancode, action)`
- `InputState`: held (persistent), pressed/released (per-frame, reset in `begin_frame()`)
- Mouse: screen coords + world coords

### Camera (`camera/camera.h/cpp`)
- `CameraState`: position, zoom with easing, follow target, shake effect, bounds
- `CameraSystem::update(cam, dt)` — handles follow, zoom interpolation, shake
- `build_matrices(cam, aspect)` → view + projection matrices
- Default: follow_speed=5, zoom_speed=5, near=-500, far=500

### UI (`ui/imgui_ui.h/cpp`)
- ImGui integration with SDL3-GPU backends
- `ui_init/shutdown/process_event/begin_frame/end_frame/prepare_draw/draw`

### Rendering (`render/`)
- `BackgroundRenderer` — procedural starfield
- `RenderSystem` — sprite rendering
- `Sprite` — texture-based sprite

### Core (`core/`)
- `TaskSystem` — thread pool (1 worker thread), `enqueue()` + `is_idle()`
- `AssetManager` — GPU resource lifecycle

## Adding a New Engine Feature
1. Create header + source in appropriate subdirectory
2. Add source to `topo_engine` library in CMakeLists.txt
3. Include path is `src/engine/` (already set as PUBLIC include dir)
4. Initialize in `Application::on_init()`, clean up in `on_cleanup()`

## Conventions
- All headers use `#pragma once`
- GPU resources: allocate in init, release in cleanup
- No raw new/delete — SDL resource management or RAII
- ECS components must be copyable; async state uses mutexes outside ECS
- Frame flow: acquire → render → end (never skip end_frame)
