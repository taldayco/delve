---
globs: src/game/terrain/**
---

# Terrain Module Rules

## Pipeline Stages (must be followed in order)
1. **Noise** → 2. **Composition** → 3. **Contour** → 4. **Generation** → 5. **Mesh** → 6. **Rendering**

Never skip a stage. Each stage's output feeds the next.

## Critical Data Structures

### MapData (map_data.h)
Central container. All float vectors sized to `width * height`:
- `elevation`, `river_mask`, `worley`, `worley_edge`, `worley_cell_value`, `final_elevation`
- `liquid_mask`, `basalt_height` (derived)
- `terrain_map` (int16_t): per-pixel terrain type (TERRAIN_EMPTY=0, TERRAIN_BASALT=-1, TERRAIN_LAVA=-2, TERRAIN_VOID=-3)
- `columns` (HexColumn vector), `lava_bodies`, `void_bodies`, `contour_lines`, `band_map`
- Call `allocate(w, h)` before use — zeros terrain_map and resizes all vectors

### HexColumn (hex.h)
- `q, r`: axial hex coordinates
- `height`, `base_height`: vertical extent
- `visible_edges[6]`: which of 6 hex sides are exposed
- `edge_drops[6]`: height difference to neighbor

### TerrainMesh (terrain_mesh.h)
- `basalt_layers`: vector of RenderingLayer (vertices + indices)
- `lava_vertices/indices`, `contour_vertices`
- BasaltVertex: pos_x/y/z, color_r/g/b, sheen, nx/ny/nz
- GpuPointLight: 32 bytes (std430 layout, static_assert enforced)

## Hex Coordinate System
- Axial coordinates: (q, r)
- `hex_to_pixel(q, r, hex_size, &x, &y)` converts to 2D
- `pixel_to_hex(x, y, hex_size)` converts back
- `get_hex_corners(q, r, hex_size, corners[6])` returns 6 vertices
- HEX_SIZE = 8.0 world units (Config::HEX_SIZE)

## Noise Parameters
- `ElevationParams`: frequency(0.003), octaves(6), lacunarity(2.0), gain(0.5), seed(1337), scurve_bias(0.65)
- `WorleyParams`: frequency(0.015), seed(4242), jitter(1.0), warp_amp(40.0), warp_frequency(0.003), warp_octaves(3)
- `RiverParams`: frequency(0.008), octaves(4), threshold(0.7)

## Palettes
- `Palette`: name + colors[6] (uint32_t ARGB)
- Height h is normalized [0, 1]
- `get_elevation_color_smooth(h, palette)` lerps 5 bands over [0,1]
- `organic_color(h, x, y, palette)` adds noise variation

## Contour System
- `extract_contours()` fills contour lines and band_map (per-pixel elevation band index)
- `detect_plateaus()` modifies terrain_map in-place
- Band_map indices must match plateau count

## Terrain Generation
- `TerrainGenerator::generate()` is static and stateless
- Takes heightmap + band_map → returns TerrainData (plateaus, columns, lava_bodies, terrain_map)

## Performance Notes
- Map is 1024x1024 = ~1M pixels. All operations must handle this scale.
- Terrain generation runs async via TaskSystem (1 worker thread)
- Results returned via AsyncTerrainState (mutex-protected, not ECS)
- Avoid O(N^2) or worse on per-pixel data
