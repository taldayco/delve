#pragma once
#include <cstdint>
#include <queue>
#include <vector>

template <typename Pred>
std::vector<std::vector<int>>
flood_fill_regions(int width, int height, Pred should_include,
                   int min_region_size = 0) {
  std::vector<uint8_t> visited(width * height, 0);
  std::vector<std::vector<int>> regions;
  const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  for (int sy = 0; sy < height; ++sy) {
    for (int sx = 0; sx < width; ++sx) {
      int start = sy * width + sx;
      if (visited[start] || !should_include(start))
        continue;

      std::vector<int> region;
      std::queue<int> q;
      q.push(start);
      visited[start] = 1;

      while (!q.empty()) {
        int idx = q.front();
        q.pop();
        int cx = idx % width, cy = idx / width;
        region.push_back(idx);

        for (int d = 0; d < 4; ++d) {
          int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
          if (nx < 0 || ny < 0 || nx >= width || ny >= height)
            continue;
          int nidx = ny * width + nx;
          if (!visited[nidx] && should_include(nidx)) {
            visited[nidx] = 1;
            q.push(nidx);
          }
        }
      }

      if ((int)region.size() >= min_region_size)
        regions.push_back(std::move(region));
    }
  }

  return regions;
}
