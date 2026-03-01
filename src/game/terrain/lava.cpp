#include "terrain/lava.h"
#include "terrain/basalt.h"
#include "terrain/color.h"
#include "terrain/map_data.h"
#include "config.h"
#include "terrain/terrain_generator.h"
#include "terrain/util.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <queue>
#include <random>
#include <unordered_set>
#include <vector>
struct P2 {
  float x, y;
};

static float poly_area(const std::vector<P2> &P) {
  double A = 0.0;
  size_t n = P.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++)
    A += (double)(P[j].x * P[i].y - P[i].x * P[j].y);
  return (float)(A * 0.5);
}

static bool point_in_tri(const P2 &p, const P2 &a, const P2 &b, const P2 &c) {
  float v0x = c.x - a.x, v0y = c.y - a.y;
  float v1x = b.x - a.x, v1y = b.y - a.y;
  float v2x = p.x - a.x, v2y = p.y - a.y;
  float d00 = v0x * v0x + v0y * v0y;
  float d01 = v0x * v1x + v0y * v1y;
  float d11 = v1x * v1x + v1y * v1y;
  float d20 = v2x * v0x + v2y * v0y;
  float d21 = v2x * v1x + v2y * v1y;
  float denom = d00 * d11 - d01 * d01;
  if (std::fabs(denom) < 1e-12f)
    return false;
  float v = (d11 * d20 - d01 * d21) / denom;
  float w = (d00 * d21 - d01 * d20) / denom;
  float u = 1.0f - v - w;
  return u >= 0.0f && v >= 0.0f && w >= 0.0f;
}

static bool is_ear(int i0, int i1, int i2, const std::vector<int> &idx,
                   const std::vector<P2> &P) {
  const P2 &a = P[idx[i0]];
  const P2 &b = P[idx[i1]];
  const P2 &c = P[idx[i2]];
  float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  if (cross <= 0.0f)
    return false;
  for (size_t k = 0; k < idx.size(); ++k) {
    if ((int)k == i0 || (int)k == i1 || (int)k == i2)
      continue;
    if (point_in_tri(P[idx[k]], a, b, c))
      return false;
  }
  return true;
}

static void triangulate_ear_clipping(const std::vector<P2> &P,
                                     std::vector<int> &tri_indices) {
  tri_indices.clear();
  if (P.size() < 3)
    return;
  std::vector<int> idx(P.size());
  for (size_t i = 0; i < P.size(); ++i)
    idx[i] = (int)i;
  if (poly_area(P) < 0.0f)
    std::reverse(idx.begin(), idx.end());
  int guard = 0;
  while (idx.size() > 3 && guard < 100000) {
    bool clipped = false;
    for (size_t i = 0; i < idx.size(); ++i) {
      int i0 = (int)((i + idx.size() - 1) % idx.size());
      int i1 = (int)i;
      int i2 = (int)((i + 1) % idx.size());
      if (is_ear(i0, i1, i2, idx, P)) {
        tri_indices.push_back(idx[i0]);
        tri_indices.push_back(idx[i1]);
        tri_indices.push_back(idx[i2]);
        idx.erase(idx.begin() + i1);
        clipped = true;
        break;
      }
    }
    if (!clipped)
      break;
    ++guard;
  }
  if (idx.size() == 3) {
    tri_indices.push_back(idx[0]);
    tri_indices.push_back(idx[1]);
    tri_indices.push_back(idx[2]);
  }
}

static void trace_outline_4connected(const std::vector<uint8_t> &mask, int W,
                                     int H, std::vector<P2> &out_poly) {
  out_poly.clear();
  int sx = -1, sy = -1;
  for (int y = 0; y < H && sy < 0; ++y)
    for (int x = 0; x < W; ++x)
      if (mask[(size_t)y * W + x]) {
        sx = x;
        sy = y;
        break;
      }
  if (sx < 0)
    return;
  int dirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  int cx = sx, cy = sy, cd = 0;
  auto inside = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H;
  };
  auto filled = [&](int x, int y) {
    return inside(x, y) && mask[(size_t)y * W + x];
  };
  int loop_guard = 0;
  do {
    int left = (cd + 3) & 3;
    int lx = cx + dirs[left][0];
    int ly = cy + dirs[left][1];
    if (filled(lx, ly)) {
      cd = left;
      cx = lx;
      cy = ly;
    } else {
      int fx = cx + dirs[cd][0];
      int fy = cy + dirs[cd][1];
      if (filled(fx, fy)) {
        cx = fx;
        cy = fy;
      } else {
        cd = (cd + 1) & 3;
      }
    }
    float vx = cx + 0.5f;
    float vy = cy + 0.5f;
    if (out_poly.empty() || std::fabs(out_poly.back().x - vx) > 1e-4f ||
        std::fabs(out_poly.back().y - vy) > 1e-4f) {
      out_poly.push_back({vx, vy});
    }
    if (++loop_guard > W * H * 8)
      break;
  } while (!(cx == sx && cy == sy && out_poly.size() > 2));
  if (out_poly.size() >= 3 && poly_area(out_poly) < 0.0f)
    std::reverse(out_poly.begin(), out_poly.end());
}

static void generate_lava_grid_mesh(LavaBody &lava, int width, int height, float grid_spacing) {
  lava.mesh.vertices.clear();
  lava.mesh.indices.clear();

  if (lava.pixels.empty()) return;

  if (lava.pixel_set.empty()) {
    for (int idx : lava.pixels) lava.pixel_set.insert(idx);
  }

  auto is_lava = [&](float x, float y) {
    int ix = (int)std::round(x);
    int iy = (int)std::round(y);
    if (ix < 0 || ix >= width || iy < 0 || iy >= height) return false;
    return lava.pixel_set.count(iy * width + ix) > 0;
  };


  int nx = (int)std::ceil((lava.max_x - lava.min_x) / grid_spacing) + 1;
  int ny = (int)std::ceil((lava.max_y - lava.min_y) / grid_spacing) + 1;

  // Guard against pathologically large lava bodies that would allocate
  // hundreds of MB and freeze/OOM the system.
  constexpr int MAX_LAVA_GRID_CELLS = 200 * 200; // ~40k cells max
  if (nx * ny > MAX_LAVA_GRID_CELLS) {
    lava.pixel_set.clear();
    { std::unordered_set<int> empty; lava.pixel_set.swap(empty); }
    return;
  }

  std::vector<int> vertex_map(nx * ny, -1);

  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      float wx = lava.min_x + i * grid_spacing;
      float wy = lava.min_y + j * grid_spacing;



      if (is_lava(wx, wy)) {
        vertex_map[j * nx + i] = (int)lava.mesh.vertices.size();
        lava.mesh.vertices.push_back({wx, wy, lava.height});
      }
    }
  }

  for (int j = 0; j < ny - 1; ++j) {
    for (int i = 0; i < nx - 1; ++i) {
      int i00 = vertex_map[j * nx + i];
      int i10 = vertex_map[j * nx + (i + 1)];
      int i01 = vertex_map[(j + 1) * nx + i];
      int i11 = vertex_map[(j + 1) * nx + (i + 1)];

      if (i00 != -1 && i10 != -1 && i01 != -1) {
        lava.mesh.indices.push_back(i00);
        lava.mesh.indices.push_back(i10);
        lava.mesh.indices.push_back(i01);
      }
      if (i10 != -1 && i11 != -1 && i01 != -1) {
        lava.mesh.indices.push_back(i10);
        lava.mesh.indices.push_back(i11);
        lava.mesh.indices.push_back(i01);
      }
    }
  }

  // pixel_set is only needed during mesh generation — free it now so that
  // destructing a LavaBody on the main thread does not stall the render loop.
  lava.pixel_set.clear();
  { std::unordered_set<int> empty; lava.pixel_set.swap(empty); }
}

static void build_triangle_mesh_from_polygon(const std::vector<P2> &poly,
                                             float z, LavaMesh &mesh_out) {

  mesh_out.vertices.clear();
  mesh_out.indices.clear();
  mesh_out.grid_width = 0;
  mesh_out.grid_height = 0;
  mesh_out.active.clear();
  if (poly.size() < 3)
    return;
  std::vector<int> tri_idx;
  triangulate_ear_clipping(poly, tri_idx);
  if (tri_idx.empty())
    return;
  mesh_out.vertices.reserve(poly.size());
  for (const auto& p : poly) {
      mesh_out.vertices.push_back({p.x, p.y, z});
  }
  for (int idx : tri_idx) {
      mesh_out.indices.push_back((uint32_t)idx);
  }
}

std::vector<ChannelRegion>
extract_channel_spaces(std::span<const int16_t> terrain_map, int width,
                       int height, std::span<const float> heightmap) {

  SDL_Log("Phase 1.1: Extracting channel spaces from terrain_map");

  int total_pixels = width * height;
  int basalt_pixels = 0;
  for (int i = 0; i < total_pixels; ++i)
    if (terrain_map[i] == TERRAIN_BASALT)
      basalt_pixels++;
  int channel_pixels = total_pixels - basalt_pixels;
  SDL_Log("  Basalt pixels: %d / %d (%.1f%%)", basalt_pixels, total_pixels,
          100.0f * basalt_pixels / total_pixels);
  SDL_Log("  Channel pixels: %d / %d (%.1f%%)", channel_pixels, total_pixels,
          100.0f * channel_pixels / total_pixels);

  std::vector<uint8_t> visited(width * height, 0);
  std::vector<ChannelRegion> regions;
  const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  for (int sy = 0; sy < height; ++sy) {
    for (int sx = 0; sx < width; ++sx) {
      int start = sy * width + sx;
      if (terrain_map[start] == TERRAIN_BASALT || visited[start])
        continue;

      float base_elevation = heightmap[start];

      ChannelRegion region;
      std::queue<int> q;
      q.push(start);
      visited[start] = 1;

      float min_x = sx, max_x = sx, min_y = sy, max_y = sy;

      while (!q.empty()) {
        int idx = q.front();
        q.pop();
        int cx = idx % width, cy = idx / width;
        region.pixels.push_back(idx);

        min_x = std::min(min_x, (float)cx);
        max_x = std::max(max_x, (float)cx);
        min_y = std::min(min_y, (float)cy);
        max_y = std::max(max_y, (float)cy);

        for (auto [dx, dy] : dirs) {
          int nx = cx + dx, ny = cy + dy;
          if (nx < 0 || ny < 0 || nx >= width || ny >= height)
            continue;
          int nidx = ny * width + nx;

          if (!visited[nidx] && terrain_map[nidx] != TERRAIN_BASALT) {
            float elevation_diff = std::abs(heightmap[nidx] - base_elevation);

            if (elevation_diff < 0.035f || elevation_diff > 0.1f) {
              visited[nidx] = 1;
              q.push(nidx);
            }
          }
        }
      }

      region.min_x = min_x;
      region.max_x = max_x;
      region.min_y = min_y;
      region.max_y = max_y;
      region.avg_elevation = base_elevation;

      float w = max_x - min_x + 1;
      float h = max_y - min_y + 1;
      region.aspect_ratio = std::max(w, h) / std::max(1.0f, std::min(w, h));

      regions.push_back(std::move(region));
    }
  }

  SDL_Log("  Found %zu connected channel regions", regions.size());

  if (!regions.empty()) {
    std::vector<int> sizes;
    for (const auto &r : regions)
      sizes.push_back(r.pixels.size());
    std::sort(sizes.rbegin(), sizes.rend());

    SDL_Log("  Top 10 region sizes:");
    for (int i = 0; i < std::min(10, (int)sizes.size()); ++i) {
      SDL_Log("    #%d: %d pixels", i + 1, sizes[i]);
    }

    SDL_Log("  Aspect ratios:");
    for (int i = 0; i < std::min(10, (int)regions.size()); ++i) {
      SDL_Log("    Region #%d: aspect=%.2f, size=%d", i + 1,
              regions[i].aspect_ratio, (int)regions[i].pixels.size());
    }
  }

  return regions;
}
std::vector<ChannelRegion>
subdivide_large_regions(const std::vector<ChannelRegion> &regions,
                        const std::vector<HexColumn> &columns, int width,
                        int height) {

  std::vector<ChannelRegion> result;

  for (const auto &region : regions) {
    if (region.pixels.size() < 50000) {
      result.push_back(region);
      continue;
    }

    std::vector<uint8_t> mask(width * height, 0);
    for (int idx : region.pixels) {
      mask[idx] = 1;
    }

    std::vector<int> channel_pixels;

    for (int idx : region.pixels) {
      int x = idx % width;
      int y = idx / width;

      float min_dist = 1e9f;
      for (const auto &col : columns) {
        Vec2 corners[6];
        get_hex_corners(col.q, col.r, Config::HEX_SIZE, corners);

        float cx = 0, cy = 0;
        for (int i = 0; i < 6; ++i) {
          cx += corners[i].x;
          cy += corners[i].y;
        }
        cx /= 6.0f;
        cy /= 6.0f;

        float dist = std::hypot(x - cx, y - cy);
        min_dist = std::min(min_dist, dist);
      }

      if (min_dist < Config::HEX_SIZE * 3.0f) {
        channel_pixels.push_back(idx);
      }
    }

    if (channel_pixels.size() > 1000) {
      ChannelRegion sub;
      sub.pixels = channel_pixels;
      result.push_back(sub);
    }
  }

  return result;
}
static void fill_holes_in_region(ChannelRegion &region, int width, int height) {
  std::unordered_set<int> pixel_set(region.pixels.begin(), region.pixels.end());

  int min_x = width, max_x = 0, min_y = height, max_y = 0;
  for (int idx : region.pixels) {
    int x = idx % width, y = idx / width;
    min_x = std::min(min_x, x);
    max_x = std::max(max_x, x);
    min_y = std::min(min_y, y);
    max_y = std::max(max_y, y);
  }

  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      int idx = y * width + x;
      if (pixel_set.count(idx))
        continue;

      int count = 0;
      int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto [dx, dy] : dirs) {
        int nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
          if (pixel_set.count(ny * width + nx))
            count++;
        }
      }

      if (count >= 3) {
        region.pixels.push_back(idx);
        pixel_set.insert(idx);
      }
    }
  }
}
std::vector<ChannelRegion>
filter_lava_channels(const std::vector<ChannelRegion> &regions,
                      std::span<const float> heightmap, int width, int height) {

  std::vector<ChannelRegion> candidates;

  for (const auto &region : regions) {
    float sum_h = 0;
    for (int idx : region.pixels)
      sum_h += heightmap[idx];
    float avg_h = sum_h / region.pixels.size();

    bool touches_boundary = (region.min_x <= 1 || region.max_x >= width - 2 ||
                             region.min_y <= 1 || region.max_y >= height - 2);

    bool is_river = region.aspect_ratio > 2.0f && region.pixels.size() > 800;
    bool is_pool = region.pixels.size() > 300 && region.pixels.size() < 5000;
    bool is_lake = region.pixels.size() > 2000;

    bool low_elevation = avg_h < 0.5f;
    bool interior = !touches_boundary;

    if (interior && low_elevation && (is_river || is_pool || is_lake)) {
      candidates.push_back(region);
    }
  }

  SDL_Log("Phase 3.1: Filtered %zu lava channel candidates from %zu regions",
          candidates.size(), regions.size());

  if (!candidates.empty()) {
    SDL_Log("  Selected channels:");
    for (size_t i = 0; i < std::min((size_t)5, candidates.size()); ++i) {
      float sum_h = 0;
      for (int idx : candidates[i].pixels)
        sum_h += heightmap[idx];
      float avg_h = sum_h / candidates[i].pixels.size();

      SDL_Log("    #%zu: aspect=%.2f, size=%d, elev=%.3f", i + 1,
              candidates[i].aspect_ratio, (int)candidates[i].pixels.size(),
              avg_h);
    }
  }
  for (auto &candidate : candidates) {
    fill_holes_in_region(candidate, width, height);
  }

  return candidates;
}

static void densify_region(std::vector<int> &pixels, int width, int height) {
  std::unordered_set<int> pixel_set(pixels.begin(), pixels.end());
  std::vector<int> to_add;

  for (int idx : pixels) {
    int x = idx % width;
    int y = idx / width;

    int neighbors[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto [dx, dy] : neighbors) {
      int nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
        int nidx = ny * width + nx;
        if (!pixel_set.count(nidx)) {
          to_add.push_back(nidx);
          pixel_set.insert(nidx);
        }
      }
    }
  }

  pixels.insert(pixels.end(), to_add.begin(), to_add.end());
}
static LavaBody channel_to_lava_body(const ChannelRegion &channel,
                                       std::span<const float> heightmap,
                                       int width, int height, int channel_idx) {
  std::vector<uint8_t> mask(width * height, 0);
  for (int idx : channel.pixels) {
    if (idx >= 0 && idx < width * height) {
      mask[idx] = 1;
    }
  }

  float sum_h = 0;
  for (int idx : channel.pixels) {
    sum_h += heightmap[idx];
  }
  float avg_h = sum_h / channel.pixels.size();

  std::vector<P2> poly;
  trace_outline_4connected(mask, width, height, poly);

  if (poly.size() < 3) {
    SDL_Log("Channel %d: Failed to trace outline", channel_idx);
    return {};
  }

  LavaBody lava;
  lava.plateau_index = -1;
  lava.height = avg_h - 0.15f;
  lava.min_x = channel.min_x;
  lava.max_x = channel.max_x;
  lava.min_y = channel.min_y;
  lava.max_y = channel.max_y;
  lava.aspect_ratio = channel.aspect_ratio;
  lava.pixels = channel.pixels;
  densify_region(lava.pixels, width, height);
  lava.time_offset = (hash1d(channel_idx) % 1000) / 1000.0f * 6.283185f;

  generate_lava_grid_mesh(lava, width, height, 2.0f);

  SDL_Log("Channel %d: Created lava body with %zu vertices", channel_idx,
          lava.mesh.vertices.size());

  return lava;
}

std::vector<LavaBody>
channels_to_lava_bodies(const std::vector<ChannelRegion> &channels,
                         std::span<const float> heightmap, int width,
                         int height) {

  std::vector<LavaBody> lava_bodies;

  for (size_t i = 0; i < channels.size(); ++i) {
    LavaBody lava =
        channel_to_lava_body(channels[i], heightmap, width, height, (int)i);
    if (!lava.mesh.vertices.empty()) {
      lava_bodies.push_back(std::move(lava));
    }
  }

  SDL_Log("Created %zu lava bodies from %zu channels", lava_bodies.size(),
          channels.size());
  return lava_bodies;
}
std::vector<LavaBody>
identify_lava_bodies(std::span<const float> heightmap, int width, int height,
                      const std::vector<Plateau> &plateaus,
                      const std::vector<int> &plateaus_with_columns) {
  std::unordered_set<int> used(plateaus_with_columns.begin(),
                               plateaus_with_columns.end());
  float min_plateau_h = 1e9f;
  for (const auto &p : plateaus)
    min_plateau_h = std::min(min_plateau_h, p.height);

  std::vector<int> candidates;
  for (size_t i = 0; i < plateaus.size(); ++i)
    if (!used.count((int)i) && !plateaus[i].pixels.empty())
      candidates.push_back((int)i);

  if (candidates.empty()) {
    SDL_Log("Lava: no unused plateaus available");
    return {};
  }

  uint32_t seed = 1469598103u;
  seed ^= (uint32_t)width + 0x9e3779b9u + (seed << 6) + (seed >> 2);
  seed ^= (uint32_t)height + 0x9e3779b9u + (seed << 6) + (seed >> 2);
  seed ^= (uint32_t)plateaus.size() + 0x9e3779b9u + (seed << 6) + (seed >> 2);
  std::mt19937 rng(seed);
  std::shuffle(candidates.begin(), candidates.end(), rng);
  if ((int)candidates.size() > 3)
    candidates.resize(3);

  std::vector<LavaBody> out;

  for (int pi : candidates) {
    const auto &plat = plateaus[pi];

    std::vector<uint8_t> mask((size_t)width * height, 0);
    for (int idx : plat.pixels)
      if ((uint32_t)idx < mask.size())
        mask[idx] = 1;

    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (int idx : plat.pixels) {
      int x = idx % width, y = idx / width;
      min_x = std::min(min_x, (float)x);
      max_x = std::max(max_x, (float)x);
      min_y = std::min(min_y, (float)y);
      max_y = std::max(max_y, (float)y);
    }

    std::vector<P2> poly_px;
    trace_outline_4connected(mask, width, height, poly_px);
    if (poly_px.size() < 3) {
      SDL_Log("Lava: plateau %d produced no polygon outline", pi);
      continue;
    }

    float lava_h = (std::fabs(plat.height - min_plateau_h) <= 1e-4f)
                        ? plat.height
                        : (plat.height - 0.015f);

    std::vector<P2> poly_world;
    poly_world.reserve(poly_px.size());
    for (const auto &p : poly_px)
      poly_world.push_back({p.x, p.y});

    LavaBody lava;
    lava.plateau_index = pi;
    lava.height = lava_h;
    lava.min_x = min_x;
    lava.max_x = max_x;
    lava.min_y = min_y;
    lava.max_y = max_y;
    float w = max_x - min_x + 1.f, h = max_y - min_y + 1.f;
    lava.aspect_ratio = std::max(w, h) / std::max(1.0f, std::min(w, h));
    lava.pixels = plat.pixels;
    lava.time_offset = (hash1d(pi) % 1000) / 1000.0f * 6.283185f;

    generate_lava_grid_mesh(lava, width, height, 2.0f);

    if (!lava.mesh.vertices.empty())
      out.push_back(std::move(lava));
  }

  SDL_Log("Lava: produced %zu triangle bodies from %zu unused candidates",
          out.size(), candidates.size());
  return out;
}

float get_lava_height(float x, float y, float base_z, float time,
                       float time_offset) {
  float t = time + time_offset;
  float wave1 = std::sin(x * 0.3f + t) * 0.02f;
  float wave2 = std::sin(y * 0.21f + t * 1.3f) * 0.015f;
  float wave3 = std::sin((x + y) * 0.15f + t * 0.8f) * 0.01f;
  return base_z + wave1 + wave2 + wave3;
}



FloodFillResult generate_lava_and_void(MapData &data, float void_chance, int seed) {
  int width = data.width;
  int height = data.height;
  int n = width * height;

  std::vector<bool> visited(n, false);
  FloodFillResult result;

  const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};


  uint32_t rng_seed = 0xDEADBEEFu;
  rng_seed ^= (uint32_t)width + 0x9e3779b9u + (rng_seed << 6) + (rng_seed >> 2);
  rng_seed ^= (uint32_t)height + 0x9e3779b9u + (rng_seed << 6) + (rng_seed >> 2);
  rng_seed ^= (uint32_t)seed + 0x9e3779b9u + (rng_seed << 6) + (rng_seed >> 2);
  std::mt19937 rng(rng_seed);
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  int body_index = 0;

  for (int sy = 0; sy < height; ++sy) {
    for (int sx = 0; sx < width; ++sx) {
      int start = sy * width + sx;
      if (visited[start] || data.terrain_map[start] == TERRAIN_BASALT)
        continue;

      std::vector<int> component_pixels;
      std::queue<int> q;
      q.push(start);
      visited[start] = true;

      float mn_x = (float)sx, mx_x = (float)sx;
      float mn_y = (float)sy, mx_y = (float)sy;

      while (!q.empty()) {
        int idx = q.front();
        q.pop();
        component_pixels.push_back(idx);
        int cx = idx % width, cy = idx / width;

        mn_x = std::min(mn_x, (float)cx);
        mx_x = std::max(mx_x, (float)cx);
        mn_y = std::min(mn_y, (float)cy);
        mx_y = std::max(mx_y, (float)cy);

        for (auto [dx, dy] : dirs) {
          int nx = cx + dx, ny = cy + dy;
          if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            int nidx = ny * width + nx;
            if (!visited[nidx] && data.terrain_map[nidx] != TERRAIN_BASALT) {
              visited[nidx] = true;
              q.push(nidx);
            }
          }
        }
      }

      if (component_pixels.size() < 50)
        continue;

      bool is_void = dist(rng) < void_chance;
      int16_t terrain_type = is_void ? TERRAIN_VOID : TERRAIN_LAVA;

      LavaBody body;
      body.plateau_index = -1;
      body.height = 0.0f;
      body.min_x = mn_x;
      body.max_x = mx_x;
      body.min_y = mn_y;
      body.max_y = mx_y;
      float bw = mx_x - mn_x + 1.f, bh = mx_y - mn_y + 1.f;
      body.aspect_ratio = std::max(bw, bh) / std::max(1.0f, std::min(bw, bh));
      body.pixels = std::move(component_pixels);
      body.time_offset =
          (hash1d(body_index++) % 1000) / 1000.0f * 6.283185f;

      if (!is_void) {
        generate_lava_grid_mesh(body, width, height, 2.0f);
      }

      for (int idx : body.pixels) {
        if (data.terrain_map[idx] != TERRAIN_BASALT)
          data.terrain_map[idx] = terrain_type;
      }

      if (is_void)
        result.void_bodies.push_back(std::move(body));
      else
        result.lava_bodies.push_back(std::move(body));
    }
  }

  SDL_Log("generate_lava_and_void: %zu lava bodies, %zu void bodies",
          result.lava_bodies.size(), result.void_bodies.size());
  return result;
}
