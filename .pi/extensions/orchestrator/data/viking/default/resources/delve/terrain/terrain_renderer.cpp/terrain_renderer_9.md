global_index_ssbo = gpu_create_buffer(
      device,
      MAX_LIGHT_INDICES * sizeof(uint32_t),
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
      SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);


  cull_counter_ssbo = gpu_create_zeroed_buffer(
      device,
      sizeof(uint32_t),
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);


  point_light_ssbo = gpu_create_buffer(
      device,
      MAX_LIGHTS * sizeof(GpuPointLight),
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
      SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);

  cluster_grid_w = tilesX;
  cluster_grid_y = tilesY;

  if (asset_manager) {
    asset_manager->register_buffer("point_light_ssbo",  point_light_ssbo);
    asset_manager->register_buffer("cluster_aabb_ssbo", cluster_aabb_ssbo);
    asset_manager->register_buffer("light_grid_ssbo",   light_grid_ssbo);
    asset_manager->register_buffer("global_index_ssbo", global_index_ssbo);
    asset_manager->register_buffer("cull_counter_ssbo", cull_counter_ssbo);
  }

  SDL_Log("TerrainRenderer: Cluster buffers created (%u×%u×%u clusters)",
          tilesX, tilesY, num_slices);
}




void TerrainRenderer::upload_mesh(SDL_GPUDevice *device, const TerrainMesh &mesh) {
  // Caller is responsible for SDL_WaitForGPUIdle before calling this.
  release_buffers(device);

  // --- Gather all CPU data first so we can size transfer buffers exactly ---

  std::vector<BasaltVertex> all_verts;
  std::vector<uint32_t>     all_indices;
  basalt_side_index_count  = 0;
  basalt_total_index_count = 0;

  if (!mesh.basalt_layers.empty() && !mesh.basalt_layers[0].vertices.empty()) {
    uint32_t vo = (uint32_t)all_verts.size();
    all_verts.insert(all_verts.end(),
                     mesh.basalt_layers[0].vertices.begin(),
                     mesh.basalt_layers[0].vertices.end());
    for (uint32_t idx : mesh.basalt_layers[0].indices)
      all_indices.push_back(idx + vo);
    basalt_side_index_count = (uint32_t)mesh.basalt_layers[0].indices.size();
  }
  if (mesh.basalt_layers.size() > 1 && !mesh.basalt_layers[1].vertices.empty()) {
    uint32_t vo = (uint32_t)all_verts.size();
    all_verts.insert(all_verts.end(),
                     mesh.basalt_layers[1].vertices.begin(),
                     mesh.basalt_layers[1].vertices.end());
    for (uint32_t idx : mesh.basalt_layers[1].indices)
      all_indices.push_back(idx + vo);
  }
  basalt_total_index_count = (uint32_t)all_indices.size();

  // --- Compute total staging size and create one shared transfer buffer ---

  uint32_t basalt_vbo_sz    = (uint32_t)(all_verts.size()                    * sizeof(BasaltVertex));
  uint32_t basalt_ibo_sz    = (uint32_t)(all_indices.size()                  * sizeof(uint32_t));
  uint32_t lava_vbo_sz      = (uint32_t)(mesh.lava_vertices.size()           * sizeof(GpuLavaVertex));
  uint32_t lava_ibo_sz      = (uint32_t)(mesh.lava_indices.size()            * sizeof(uint32_t));
  uint32_t contour_vbo_sz   = (uint32_t)(mesh.contour_vertices.size()        * sizeof(ContourVertex));

  // Align each section to 4 bytes so GPU buffer offsets are valid.
  auto align4 = [](uint32_t v) { return (v + 3u) & ~3u; };

  uint32_t off_basalt_vbo  = 0;
  uint32_t off_basalt_ibo  = off_basalt_vbo  + align4(basalt_vbo_sz);
  uint32_t off_lava_vbo    = off_basalt_ibo  + align4(basalt_ibo_sz);
  uint32_t off_lava_ibo    = off_lava_vbo    + align4(lava_vbo_sz);
  uint32_t off_contour_vbo = off_lava_ibo    + align4(lava_ibo_sz);
  uint32_t total_sz        = off_contour_vbo + align4(contour_vbo_sz);

  if (total_sz == 0) {
    has_data = false;
    return;
  }

  // Hard cap: refuse to upload meshes that would blow out GPU/CPU memory.
  constexpr uint32_t MAX_TRANSFER_SZ = 128u * 1024u * 1024u; // 128 MB
  if (total_sz > MAX_TRANSFER_SZ) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "TerrainRenderer::upload_mesh: mesh too large (%u bytes > %u limit), skipping",
                 total_sz, MAX_TRANSFER_SZ);
    has_data = false;
    return;
  }