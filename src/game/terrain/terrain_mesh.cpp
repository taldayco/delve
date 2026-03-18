#include "terrain/terrain_mesh.h"
#include "game_state.h"
#include "config.h"
#include "terrain/basalt.h"
#include "terrain/hex.h"
#include "terrain/lava.h"
#include "terrain/map_data.h"
#include "terrain/palettes.h"
#include "terrain/color.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

static void color_to_float(uint32_t c, float &r, float &g, float &b) {
  r = ((c >> 16) & 0xFF) / 255.0f;
  g = ((c >>  8) & 0xFF) / 255.0f;
  b = (c         & 0xFF) / 255.0f;
}

static void add_hex_top(const Vec2 corners[6], float z,
                        float cr, float cg, float cb, float sheen,
                        TerrainMesh::RenderingLayer &layer) {
  uint32_t base = (uint32_t)layer.vertices.size();
  for (int i = 0; i < 6; ++i) {
    float wx = corners[i].x / Config::HEX_SIZE;
    float wy = corners[i].y / Config::HEX_SIZE;
    layer.vertices.push_back({wx, wy, z, cr, cg, cb, sheen, 0.0f, 0.0f, 1.0f});
  }
  for (int i = 1; i <= 4; ++i) {
    layer.indices.push_back(base);
    layer.indices.push_back(base + i);
    layer.indices.push_back(base + i + 1);
  }
}

static void add_side_face(const Vec2 &corner0, const Vec2 &corner1,
                          float top_height, float bottom_height,
                          float cr, float cg, float cb, float sheen,
                          TerrainMesh::RenderingLayer &layer) {
  if (top_height - bottom_height < 0.01f)
    return;

  float wx0 = corner0.x / Config::HEX_SIZE;
  float wy0 = corner0.y / Config::HEX_SIZE;
  float wx1 = corner1.x / Config::HEX_SIZE;
  float wy1 = corner1.y / Config::HEX_SIZE;

  float edx = wx1 - wx0;
  float edy = wy1 - wy0;
  float nlen = std::sqrt(edx * edx + edy * edy);
  float nx = 0.0f, ny = 0.0f;
  if (nlen > 1e-6f) {
    nx =  edy / nlen;
    ny = -edx / nlen;
  }

  float side_sheen = sheen * 0.4f;

  uint32_t base = (uint32_t)layer.vertices.size();
  layer.vertices.push_back({wx0, wy0, top_height,    cr, cg, cb, side_sheen, nx, ny, 0.0f});
  layer.vertices.push_back({wx1, wy1, top_height,    cr, cg, cb, side_sheen, nx, ny, 0.0f});
  layer.vertices.push_back({wx1, wy1, bottom_height, cr, cg, cb, side_sheen, nx, ny, 0.0f});
  layer.vertices.push_back({wx0, wy0, bottom_height, cr, cg, cb, side_sheen, nx, ny, 0.0f});

  layer.indices.push_back(base);
  layer.indices.push_back(base + 1);
  layer.indices.push_back(base + 2);
  layer.indices.push_back(base);
  layer.indices.push_back(base + 2);
  layer.indices.push_back(base + 3);
}

TerrainMesh build_terrain_mesh(const TerrainState &terrain, const MapData &map_data,
                               const ContourData &contours) {
  TerrainMesh mesh;

  const auto &columns    = map_data.columns;
  const auto &lava_bodies = map_data.lava_bodies;

  if (columns.empty()) {
    SDL_Log("TerrainMesh: No columns, empty mesh");
    return mesh;
  }

  const Palette &palette = PALETTES[terrain.current_palette];

  mesh.basalt_layers.resize(2);

  for (const auto &col : columns) {
    uint32_t color = organic_color(col.base_height, col.q, col.r, palette);
    float cr, cg, cb;
    color_to_float(color, cr, cg, cb);

    Vec2 corners[6];
    get_hex_corners(col.q, col.r, Config::HEX_SIZE, corners);

    for (int i = 0; i < 6; ++i) {
      if (col.visible_edges[i]) {
        int next = (i + 1) % 6;
        float neighbor_height = col.height - col.edge_drops[i];
        add_side_face(corners[i], corners[next], col.height, neighbor_height,
                      cr, cg, cb, 1.0f, mesh.basalt_layers[0]);
      }
    }
  }

  for (const auto &col : columns) {
    uint32_t color = organic_color(col.base_height, col.q, col.r, palette);
    float cr, cg, cb;
    color_to_float(color, cr, cg, cb);

    Vec2 corners[6];
    get_hex_corners(col.q, col.r, Config::HEX_SIZE, corners);

    add_hex_top(corners, col.height, cr, cg, cb, 1.0f, mesh.basalt_layers[1]);
  }

  SDL_Log("TerrainMesh: %zu side verts, %zu side indices, %zu top verts, %zu top indices",
          mesh.basalt_layers[0].vertices.size(), mesh.basalt_layers[0].indices.size(),
          mesh.basalt_layers[1].vertices.size(), mesh.basalt_layers[1].indices.size());

  const float inv_unit = 1.0f / Config::HEX_SIZE;
  for (const auto &lava : lava_bodies) {
    uint32_t base_idx = (uint32_t)mesh.lava_vertices.size();
    for (const auto &v : lava.mesh.vertices) {
      mesh.lava_vertices.push_back({v.x * inv_unit, v.y * inv_unit,
                                    v.base_z, lava.time_offset});
    }
    for (uint32_t idx : lava.mesh.indices) {
      mesh.lava_indices.push_back(base_idx + idx);
    }
  }

  SDL_Log("TerrainMesh: %zu lava vertices, %zu lava indices",
          mesh.lava_vertices.size(), mesh.lava_indices.size());

  for (const auto &line : contours.contour_lines) {
    float wx0 = line.x1 * inv_unit;
    float wy0 = line.y1 * inv_unit;
    float wx1 = line.x2 * inv_unit;
    float wy1 = line.y2 * inv_unit;
    float z   = line.elevation;
    mesh.contour_vertices.push_back({wx0, wy0, z});
    mesh.contour_vertices.push_back({wx1, wy1, z});
  }

  SDL_Log("TerrainMesh: %zu contour vertices (%zu lines)",
          mesh.contour_vertices.size(), contours.contour_lines.size());

  return mesh;
}

SceneUniforms compute_uniforms(const MapData &map_data,
                               const glm::mat4 &view, const glm::mat4 &projection,
                               uint32_t cluster_tiles_x, uint32_t cluster_tiles_y,
                               float time, float contour_opacity,
                               uint32_t light_count,
                               float cam_x, float cam_y, float cam_z) {
  SceneUniforms u = {};

  u.view       = view;
  u.projection = projection;

  u.time             = time;
  u.contour_opacity  = contour_opacity;
  u.hex_border_width = 0.05f;

  color_to_float(Config::LAVA_COLOR, u.lava_color_r, u.lava_color_g, u.lava_color_b);

  u.star_light_r         = 0.55f;
  u.star_light_g         = 0.70f;
  u.star_light_b         = 1.00f;
  u.star_light_intensity = 0.12f;

  {
    float lx = -1.0f, ly = -1.0f, lz = 2.0f;
    float llen = std::sqrt(lx*lx + ly*ly + lz*lz);
    u.light_dir_x = lx / llen;
    u.light_dir_y = ly / llen;
    u.light_dir_z = lz / llen;
  }
  u.ambient     = 0.25f;
  u.light_col_r = 1.00f;
  u.light_col_g = 0.95f;
  u.light_col_b = 0.85f;

  u.tile_px       = 16.0f;
  u.grid_size_x   = (float)cluster_tiles_x;
  u.grid_size_y   = (float)cluster_tiles_y;
  u.num_slices    = 24.0f;
  u.near_plane    = -500.0f;
  u.far_plane     =  500.0f;
  u.light_count_f = (float)light_count;

  u.cam_world_x = cam_x;
  u.cam_world_y = cam_y;
  u.cam_world_z = cam_z;

  return u;
}
