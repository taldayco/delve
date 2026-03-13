FastNoiseLite warp(params.seed + 31337);
  warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
  warp.SetDomainWarpAmp(params.warp_amp);
  warp.SetFrequency(params.warp_frequency);
  warp.SetFractalType(FastNoiseLite::FractalType_DomainWarpProgressive);
  warp.SetFractalOctaves(params.warp_octaves);
  warp.SetFractalLacunarity(2.0f);
  warp.SetFractalGain(0.5f);


  FastNoiseLite noise_dist(params.seed);
  noise_dist.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
  noise_dist.SetCellularDistanceFunction(
      FastNoiseLite::CellularDistanceFunction_EuclideanSq);
  noise_dist.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
  noise_dist.SetFrequency(params.frequency);
  noise_dist.SetCellularJitter(params.jitter);


  FastNoiseLite noise_cell(params.seed);
  noise_cell.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
  noise_cell.SetCellularDistanceFunction(
      FastNoiseLite::CellularDistanceFunction_EuclideanSq);
  noise_cell.SetCellularReturnType(FastNoiseLite::CellularReturnType_CellValue);
  noise_cell.SetFrequency(params.frequency);
  noise_cell.SetCellularJitter(params.jitter);


  FastNoiseLite noise_edge(params.seed);
  noise_edge.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
  noise_edge.SetCellularDistanceFunction(
      FastNoiseLite::CellularDistanceFunction_EuclideanSq);
  noise_edge.SetCellularReturnType(
      FastNoiseLite::CellularReturnType_Distance2Sub);
  noise_edge.SetFrequency(params.frequency);
  noise_edge.SetCellularJitter(params.jitter);

  float ox, oy;
  seed_offset(params.seed, ox, oy);

  float min_d = 1e9f, max_d = -1e9f;
  float min_e = 1e9f, max_e = -1e9f;
  float min_c = 1e9f, max_c = -1e9f;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int idx = y * width + x;
      float wx = (float)x * params.map_scale + ox;
      float wy = (float)y * params.map_scale + oy;
      if (params.warp_amp > 0.0f)
        warp.DomainWarp(wx, wy);
      float d = noise_dist.GetNoise(wx, wy);
      float e = noise_edge.GetNoise(wx, wy);
      float c = noise_cell.GetNoise(wx, wy);
      out_value[idx] = d;
      out_edge[idx] = e;
      out_cell_value[idx] = c;
      min_d = std::min(min_d, d);
      max_d = std::max(max_d, d);
      min_e = std::min(min_e, e);
      max_e = std::max(max_e, e);
      min_c = std::min(min_c, c);
      max_c = std::max(max_c, c);
    }
  }


  float range_d = max_d - min_d;
  float range_e = max_e - min_e;
  float range_c = max_c - min_c;
  for (int i = 0; i < n; ++i) {
    out_value[i] =
        (range_d > 1e-6f) ? (out_value[i] - min_d) / range_d : 0.0f;
    out_edge[i] =
        (range_e > 1e-6f) ? (out_edge[i] - min_e) / range_e : 0.0f;
    out_cell_value[i] =
        (range_c > 1e-6f) ? (out_cell_value[i] - min_c) / range_c : 0.0f;
  }
}