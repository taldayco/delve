---
globs: src/shaders/**
---

# Shader Module Rules

## Language & Compilation
- GLSL 4.5 compiled to SPIR-V via `glslc`
- Vertex shaders: `*.vert.glsl` → `-fshader-stage=vertex`
- Fragment shaders: `*.frag.glsl` → `-fshader-stage=fragment`
- Compute shaders: `*.comp.glsl` → `-fshader-stage=compute`
- Output: `build/shaders/*.spv`
- Shared includes in same directory: `coord.glsl`, `lighting_common.glsl`, `common.glsl`

## Adding a New Shader
1. Create `name.vert.glsl` and/or `name.frag.glsl` in `src/shaders/`
2. Add to `GRAPHICS_SHADER_SOURCES` (or `COMPUTE_SHADER_SOURCES`) in `CMakeLists.txt`
3. Shared includes added to `SHADER_INCLUDES` force recompilation of all shaders

## Vertex Attribute Layouts

### Terrain (BasaltVertex)
- pos_x, pos_y, pos_z (float)
- color_r, color_g, color_b (float)
- sheen (float)
- nx, ny, nz (float) — surface normal

### Lava (GpuLavaVertex)
- pos_x, pos_y, pos_z (float)
- time_offset (float)

### Contour (ContourVertex)
- pos_x, pos_y, pos_z (float)

## Uniform Blocks (SceneUniforms)
Large struct with careful padding. Fields include:
- view, projection (mat4)
- time, contour_opacity, hex_border_width
- lava_color (rgb), star_light (rgb + intensity)
- light_dir (xyz + ambient), light_col (rgb)
- grid_size (xy), num_slices, tile_px
- near, far planes
- light_count_f
- Multiple `_pad` fields for std140 alignment

## Light Culling (Compute)
- `generate_clusters.comp.glsl` — builds per-tile AABBs for clustered lighting
- `light_culling.comp.glsl` — assigns lights to clusters
- Max lights: 1024 (MAX_LIGHTS)
- Max light indices: 65536 (MAX_LIGHT_INDICES)
- GpuPointLight: 32 bytes std430 (pos xyz, radius, color rgb, intensity)

## Render Order
1. Background (starfield, full-screen quad)
2. Terrain basalt layers (depth write)
3. Lava bodies
4. Contour lines (depth-tested)
5. Actors

## Conventions
- Use `#include` for shared code (coord.glsl, lighting_common.glsl)
- Dithering and color quantization in terrain.frag.glsl
- Lambertian diffuse + point light contributions
- Isometric projection handled in vertex shader
