---
name: shader
description: Implement changes to GLSL shaders
when: When modifying or creating vertex, fragment, or compute shaders
---

# Shader Specialist Skill

You implement GLSL 4.5 shaders compiled to SPIR-V via glslc.

## File Conventions
- Source: `src/shaders/` with extensions `.vert.glsl`, `.frag.glsl`, `.comp.glsl`
- Output: `build/shaders/*.spv`
- Shared includes: `coord.glsl`, `lighting_common.glsl`, `common.glsl` (use `#include`)

## Creating a New Shader Pair
1. Create `src/shaders/name.vert.glsl` and `src/shaders/name.frag.glsl`
2. Add both to `GRAPHICS_SHADER_SOURCES` in CMakeLists.txt
3. For compute: add to `COMPUTE_SHADER_SOURCES`
4. If adding shared include: add to `SHADER_INCLUDES` (triggers full recompile)
5. Create corresponding GPU pipeline in C++ renderer code

## Vertex Formats

### Terrain (BasaltVertex) — 10 floats
```glsl
layout(location = 0) in vec2 in_pos;      // pos_x, pos_y
layout(location = 1) in float in_depth;    // pos_z
layout(location = 2) in vec3 in_color;     // color_r, color_g, color_b
layout(location = 3) in float in_sheen;    // sheen factor
layout(location = 4) in vec3 in_normal;    // nx, ny, nz
```

### Lava (GpuLavaVertex) — 4 floats
```glsl
layout(location = 0) in vec3 in_pos;       // pos_x, pos_y, pos_z
layout(location = 1) in float in_time_off; // time_offset for animation
```

### Contour (ContourVertex) — 3 floats
```glsl
layout(location = 0) in vec3 in_pos;       // pos_x, pos_y, pos_z
```

## Uniform Block (SceneUniforms)
Large struct with std140 padding. Key fields:
- `view`, `projection` (mat4)
- `time`, `contour_opacity`, `hex_border_width`
- Light data: `lava_color`, `star_light`, `light_dir`, `light_col`
- Cluster data: `grid_size`, `num_slices`, `tile_px`, `near`, `far`
- `light_count_f`

## Lighting Model
- Lambertian diffuse with configurable light direction
- Point light contributions (clustered, per-pixel)
- Star ambient with sheen factor
- Hex-dither color quantization in fragment shader

## Compute Shaders (Light Culling)
- `generate_clusters.comp.glsl` — builds per-tile AABBs
- `light_culling.comp.glsl` — assigns lights to clusters
- `GpuPointLight`: 32 bytes std430 (pos xyz, radius, color rgb, intensity)
- MAX_LIGHTS = 1024, MAX_LIGHT_INDICES = 65536

## Render Order
1. Background starfield (full-screen quad)
2. Terrain basalt layers (depth write on)
3. Lava bodies (animated)
4. Contour lines (depth-tested)
5. Actors
