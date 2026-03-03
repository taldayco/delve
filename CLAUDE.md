# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (first time or after CMakeLists.txt changes)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build everything (executable + shaders)
cmake --build build -j$(nproc)

# Run
./build/topogen
```

**Prerequisites:** SDL3 (system package), `glslc` (SPIR-V shader compiler)

## Project Overview

Delve is a procedural hex-grid terrain generator written in C++20. It renders isometric 3D landscapes using layered Perlin/Worley noise with real-time parameter editing via ImGui. Uses SDL3-GPU for rendering (Vulkan/Metal/DirectX12 abstraction).

## Architecture

The codebase splits into two layers:

- **`src/engine/`** — Reusable engine framework: application lifecycle (`app.h/cpp`), GPU context (`gpu/`), input (`input/`), camera (`camera/`), ECS integration via Flecs, ImGui UI (`ui/`), rendering (`render/`), asset management and task system (`core/`)
- **`src/game/`** — Game-specific code: `topo_game.h/cpp` (main Application subclass), `config.h` (constants), `terrain/` (terrain generation pipeline), `render/` (actor rendering)

### Terrain Pipeline (core of the project)

The terrain system in `src/game/terrain/` follows this data flow:

1. **Noise generation** (`noise.cpp`, `noise_layers.cpp`) — Perlin/Worley noise via FastNoiseLite
2. **Composition** (`noise_composer.cpp`) — Blends elevation + worley layers, applies river/liquid masks → `MapData`
3. **Contour detection** (`contour.cpp`) — Flood-fill to identify plateaus and elevation bands
4. **Terrain generation** (`terrain_generator.cpp`) — Creates hex columns per plateau, generates lava/void regions (`lava.cpp`)
5. **Mesh generation** (`terrain_mesh.cpp`) — Converts hex geometry to GPU-ready vertex/index buffers
6. **Rendering** (`terrain_renderer.cpp`) — Uploads mesh to GPU, renders via SPIR-V shaders

### Rendering Pipeline

Dual-window setup: tool window (ImGui parameter editor) and game window (terrain viewport). Frame order: background starfield → terrain (hex faces) → lava → contour lines → actors.

Shaders live in `src/shaders/` (GLSL 4.5), compiled to SPIR-V in `build/shaders/`. Shared includes: `coord.glsl`, `lighting_common.glsl`. Light culling uses compute shaders (`generate_clusters.comp.glsl`, `light_culling.comp.glsl`).

### Key Types

- `MapData` (`map_data.h`) — Central data structure holding elevation, worley, river/liquid masks, band_map, hex columns, lava bodies
- `HexColumn` / `HexCoord` (`hex.h`) — Hexagonal grid primitives with axial coordinates
- `TerrainMesh` (`terrain_mesh.h`) — GPU-ready vertex/index buffers with basalt layers
- `Plateau` / `ContourData` (`contour.h`) — Flood-fill results for elevation regions

### Build Targets

- `libimgui.a` — ImGui static library (SDL3/SDL-GPU backends)
- `libtopo_engine.a` — Engine static library
- `topogen` — Main executable
- `delve_tests` — Quantitative visual test binary

### Testing

```bash
# Build and run all tests
cmake --build build --target delve_tests -j$(nproc) && ./build/delve_tests
```

Tests output JSON to stdout. Test infrastructure in `src/test/`:

- `test_harness.h` — Custom test framework (DELVE*TEST macro, EXPECT*\* assertions)
- `terrain_metrics.h` — Elevation stats, water coverage, plateau counts
- `geometry_metrics.h` — Mesh validity, hex roundtrip accuracy, normal checks
- `animation_metrics.h` — Joint angles, symmetry scores, gait parameters

### Dependencies

- **SDL3** — System package (windowing, GPU)
- **Flecs v4.0.5** — ECS framework (FetchContent)
- **nlohmann/json v3.11.3** — JSON serialization (FetchContent)
- **GLM v1.0.1** — Math library (FetchContent)
- **ImGui** — Git submodule in `imgui/` with custom SDL3-GPU backends
- **FastNoiseLite** — Header-only noise library vendored in `src/game/terrain/`

### Runtime Configuration

Terrain parameters are saved/loaded from `config.json` at project root (elevation noise, worley noise, composition, display settings).

### Entry Point

`src/game/main.cpp` → creates `TopoGame` → calls `Application::run()` (60 FPS fixed timestep main loop in `src/engine/app.cpp`).
