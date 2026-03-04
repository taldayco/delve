---
name: terrain
description: Implement changes to the terrain generation pipeline
when: When modifying terrain generation, noise, hex grid, mesh building, palettes, contours, or lava
---

# Terrain Specialist Skill

You are the terrain pipeline expert. You implement changes to procedural terrain generation, hex grids, noise composition, palettes, contours, lava/void regions, and mesh building.

## Pipeline (always follow this order)

1. **Noise** (`noise.cpp`, `noise_layers.cpp`, `noise_layers.h`)
   - FastNoiseLite generates Perlin/Simplex/Worley noise
   - `generate_elevation_layer()`, `generate_river_mask()`, `generate_worley_layer()`
   - Parameters: ElevationParams, RiverParams, WorleyParams (each has frequency, octaves, seed, etc.)

2. **Composition** (`noise_composer.cpp`, `noise_composer.h`)
   - Blends elevation + worley layers, applies river/liquid masks
   - Output: fills MapData fields (final_elevation, liquid_mask, etc.)

3. **Contour** (`contour.cpp`, `contour.h`)
   - `extract_contours()` → contour lines + band_map
   - `detect_plateaus()` → plateau regions, modifies terrain_map
   - Plateau: height, pixel indices, bounding box

4. **Generation** (`terrain_generator.cpp`, `terrain_generator.h`)
   - `TerrainGenerator::generate()` — static, stateless
   - Input: heightmap + band_map → Output: TerrainData (plateaus, columns, lava, terrain_map)
   - Creates HexColumn per plateau, generates lava/void via `lava.cpp`

5. **Mesh** (`terrain_mesh.cpp`, `terrain_mesh.h`)
   - `build_terrain_mesh()` → TerrainMesh (GPU-ready vertex/index buffers)
   - BasaltVertex: position, color, sheen, normal
   - Computes normals, organizes into basalt_layers

6. **Rendering** (`terrain_renderer.cpp`, `terrain_renderer.h`)
   - `TerrainRenderer::upload_mesh()` → GPU upload
   - `TerrainRenderer::draw()` → render with shaders
   - Clustered lighting via compute shaders

## Adding a New Palette
1. Add entry to `PALETTES[]` array in `palettes.h`
2. Define 6 ARGB colors: water, sand, low-veg, mid-veg, rock, snow (bands at 0.0-0.2-0.4-0.6-0.8-1.0)
3. Height h is normalized [0, 1]; color functions lerp between bands

## Adding a New Biome/Terrain Feature
1. Add parameters to appropriate Params struct in `noise_layers.h`
2. Add generation logic in the correct pipeline stage
3. Thread through MapData — add new fields if needed, call `allocate()` to resize
4. Update mesh building to handle the new feature
5. Update renderer if new vertex attributes or pipelines needed

## Critical Invariants
- All per-pixel vectors in MapData must be sized `width * height`
- `terrain_map` values: TERRAIN_EMPTY(0), TERRAIN_BASALT(-1), TERRAIN_LAVA(-2), TERRAIN_VOID(-3)
- band_map indices must match plateau count from detect_plateaus
- Hex axial coords (q, r) — use hex_to_pixel/pixel_to_hex for conversion
- HEX_SIZE = 8.0 world units
- Map default: 1024x1024 pixels
- Terrain gen is async via TaskSystem — results go through AsyncTerrainState (mutex)

## Common Patterns
- Color: `organic_color(h, x, y, palette)` for noise-varied terrain color
- Hex corners: `get_hex_corners(q, r, hex_size, corners[6])` returns 6 vertices
- Edge visibility: `compute_visible_edges(columns)` marks exposed hex sides
- Lava/void: generated in `lava.cpp`, flood-fill based
