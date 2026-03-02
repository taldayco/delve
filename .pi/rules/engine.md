---
globs: src/engine/**
---

# Engine Module Rules

## Application Lifecycle
`Application` is the base class. Override these pure virtual methods:
- `on_init(gpu, ecs)` — setup resources and ECS systems
- `on_event(event, ecs)` — handle SDL events
- `on_render_tool(gpu, frame, ecs)` — render editor/tool window
- `on_render_game(gpu, frame, ecs)` — render game viewport
- `on_cleanup(ecs)` — release all resources

Optional: `on_pre_frame_game()`, `wants_game_window_open()`, `wants_game_window_close()`

Main loop runs at 60 FPS fixed timestep (dt = 0.0167s, max frame clamp 0.25s).

## GPU Context
- `GpuContext` owns the SDL_GPUDevice, main window, optional game window, and UploadManager
- `FrameContext` is per-frame: command buffer, swapchain texture, render pass
- Frame flow: `gpu_acquire_frame()` → render → `gpu_end_frame()`
- Game window is optional, created on demand via `gpu_create_game_window()`
- `UploadManager`: persistent mapped staging buffer, call `reset()` each frame, `alloc()` for suballocations

## ECS (Flecs)
- Components must be copyable (Flecs requirement)
- Systems registered in `on_init()` with phase ordering (PreUpdate, PostUpdate)
- Use `ecs.entity()` for entities, `ecs.system()` for systems
- Async state (mutexes, atomics) must NOT be ECS components — hold them in the Application subclass

## Input System
- `Action` enum: MoveUp/Down/Left/Right, Interact, Cancel, Pause, ZoomIn/Out, CameraUp/Down/Left/Right
- `InputState`: held/pressed/released arrays (pressed/released reset each frame via `begin_frame()`)
- Mouse has both screen coords (mouse_x/y) and world coords (mouse_world_x/y)
- Bind keys via `InputSystem::bind(SDL_Scancode, Action)`

## Camera System
- `CameraState`: world position, zoom with easing, follow target, screen shake, clamped bounds
- `CameraMatrices`: view + projection from `build_matrices(cam, aspect)`
- zoom clamped to [min_zoom, max_zoom], position clamped to [min/max x/y]
- follow_speed and zoom_speed default to 5.0

## Conventions
- All engine headers use `#pragma once`
- GPU resources allocated in `init()`, released in `cleanup()`
- No raw `new`/`delete` — use RAII or SDL resource management
- Texture handles bundle texture + sampler + dimensions
