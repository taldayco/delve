#include "terrain/terrain_generator.h"
#include "terrain/basalt.h"
#include "config.h"
#include "terrain/lava.h"
#include <SDL3/SDL.h>

TerrainGenerator::TerrainData
TerrainGenerator::generate(std::span<const float> heightmap,
                           std::span<const int> band_map, int width,
                           int height) {

  TerrainData data;

  data.terrain_map.assign(width * height, TERRAIN_EMPTY);

  data.plateaus = detect_plateaus(band_map, heightmap, width, height,
                                  data.terrain_map);
  SDL_Log("TerrainGenerator: Found %zu plateaus", data.plateaus.size());

  data.columns = generate_basalt_columns(heightmap, width, height,
                                          Config::HEX_SIZE, data.plateaus,
                                          data.plateaus_with_columns,
                                          data.terrain_map);
  SDL_Log("TerrainGenerator: Generated %zu columns on %zu plateaus",
          data.columns.size(), data.plateaus_with_columns.size());

  auto channel_regions =
      extract_channel_spaces(data.terrain_map, width, height, heightmap);
  SDL_Log("TerrainGenerator: Found %zu channel regions",
          channel_regions.size());

  auto lava_channels =
      filter_lava_channels(channel_regions, heightmap, width, height);
  SDL_Log("TerrainGenerator: Selected %zu lava channels",
          lava_channels.size());
  data.lava_bodies =
      channels_to_lava_bodies(lava_channels, heightmap, width, height);
  SDL_Log("TerrainGenerator: Created %zu lava bodies",
          data.lava_bodies.size());

  for (const auto& wb : data.lava_bodies)
    for (int idx : wb.pixels)
      if (idx >= 0 && idx < width * height)
        data.terrain_map[idx] = TERRAIN_LAVA;

  return data;
}
