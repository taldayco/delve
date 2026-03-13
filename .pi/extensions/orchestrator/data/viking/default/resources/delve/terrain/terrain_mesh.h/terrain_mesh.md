#pragma once
#include "terrain/map_data.h"
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

struct TerrainState;
struct ContourData;

struct BasaltVertex {
  float pos_x, pos_y, pos_z;
  float color_r, color_g, color_b;
  float sheen;
  float nx, ny, nz;
};

struct GpuLavaVertex {
  float pos_x, pos_y, pos_z;
  float time_offset;
};

struct ContourVertex {
  float pos_x, pos_y, pos_z;
};

struct SceneUniforms {
  glm::mat4 view;
  glm::mat4 projection;

  float time, contour_opacity, hex_border_width, _pad0;

  float lava_color_r, lava_color_g, lava_color_b, _pad1;

  float star_light_r, star_light_g, star_light_b, star_light_intensity;

  float light_dir_x, light_dir_y, light_dir_z, ambient;

  float light_col_r, light_col_g, light_col_b, _pad2;

  float grid_size_x, grid_size_y, num_slices, tile_px;

  float near_plane, far_plane, light_count_f, _pad4;
};

struct GpuPointLight {
  float pos_x, pos_y, pos_z, radius;
  float color_r, color_g, color_b, intensity;
};
static_assert(sizeof(GpuPointLight) == 32, "GpuPointLight must be 32 bytes for std430");

struct TerrainMesh {
  struct RenderingLayer {
    std::vector<BasaltVertex> vertices;
    std::vector<uint32_t> indices;
  };

  std::vector<RenderingLayer> basalt_layers;
  std::vector<GpuLavaVertex>  lava_vertices;
  std::vector<uint32_t>       lava_indices;
  std::vector<ContourVertex>  contour_vertices;
};

TerrainMesh build_terrain_mesh(const TerrainState &terrain, const MapData &map_data,
                               const ContourData &contours);

SceneUniforms compute_uniforms(const MapData &map_data,
                               const glm::mat4 &view, const glm::mat4 &projection,
                               uint32_t cluster_tiles_x, uint32_t cluster_tiles_y,
                               float time, float contour_opacity,
                               uint32_t light_count);
