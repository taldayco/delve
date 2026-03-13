SDL_Log("TerrainRenderer: Depth texture (re)created (%ux%u)", depth_w, depth_h);
}

void TerrainRenderer::release_buffers(SDL_GPUDevice *device) {
  auto rel = [&](SDL_GPUBuffer *&buf, const char *key) {
    if (!buf) return;
    if (asset_manager) { asset_manager->release_buffer(key); }
    else               { SDL_ReleaseGPUBuffer(device, buf); }
    buf = nullptr;
  };
  rel(basalt_vbo,  "basalt_vbo");
  rel(basalt_ibo,  "basalt_ibo");
  rel(lava_vbo,    "lava_vbo");
  rel(lava_ibo,    "lava_ibo");
  rel(contour_vbo, "contour_vbo");
  has_data = false;
}

void TerrainRenderer::release_cluster_buffers(SDL_GPUDevice *device) {
  // Release through asset manager when available so the registry stays consistent.
  auto rel = [&](SDL_GPUBuffer *&buf, const char *key) {
    if (!buf) return;
    if (asset_manager) { asset_manager->release_buffer(key); }
    else               { SDL_ReleaseGPUBuffer(device, buf); }
    buf = nullptr;
  };
  rel(point_light_ssbo,  "point_light_ssbo");
  rel(cluster_aabb_ssbo, "cluster_aabb_ssbo");
  rel(light_grid_ssbo,   "light_grid_ssbo");
  rel(global_index_ssbo, "global_index_ssbo");
  rel(cull_counter_ssbo, "cull_counter_ssbo");
  if (counter_reset_transfer) { SDL_ReleaseGPUTransferBuffer(device, counter_reset_transfer); counter_reset_transfer = nullptr; }
  cluster_grid_w = 0;
  cluster_grid_y = 0;
}

void TerrainRenderer::cleanup(SDL_GPUDevice *device) {
  SDL_WaitForGPUIdle(device);

  release_buffers(device);
  if (gltf_column_vbo) { SDL_ReleaseGPUBuffer(device, gltf_column_vbo); gltf_column_vbo = nullptr; }
  if (gltf_column_ibo) { SDL_ReleaseGPUBuffer(device, gltf_column_ibo); gltf_column_ibo = nullptr; }
  gltf_column_index_count = 0;
  release_cluster_buffers(device);

  if (dummy_ssbo)               { SDL_ReleaseGPUBuffer(device, dummy_ssbo);                           dummy_ssbo               = nullptr; }
  if (depth_texture)            { SDL_ReleaseGPUTexture(device, depth_texture);                        depth_texture            = nullptr; }
  if (terrain_pipeline)              { SDL_ReleaseGPUGraphicsPipeline(device, terrain_pipeline);              terrain_pipeline              = nullptr; }
  if (lava_pipeline)                 { SDL_ReleaseGPUGraphicsPipeline(device, lava_pipeline);                 lava_pipeline                 = nullptr; }
  if (contour_pipeline)              { SDL_ReleaseGPUGraphicsPipeline(device, contour_pipeline);              contour_pipeline              = nullptr; }
  if (instanced_terrain_pipeline)    { SDL_ReleaseGPUGraphicsPipeline(device, instanced_terrain_pipeline);    instanced_terrain_pipeline    = nullptr; }
  if (pbr_terrain_pipeline)          { SDL_ReleaseGPUGraphicsPipeline(device, pbr_terrain_pipeline);          pbr_terrain_pipeline          = nullptr; }
  if (cluster_gen_pipeline)     { SDL_ReleaseGPUComputePipeline(device, cluster_gen_pipeline);         cluster_gen_pipeline     = nullptr; }
  if (light_culling_pipeline)   { SDL_ReleaseGPUComputePipeline(device, light_culling_pipeline);       light_culling_pipeline   = nullptr; }
  // Shaders are owned by AssetManager and released via asset_manager.clear().

  initialized = false;
  SDL_Log("TerrainRenderer: Cleaned up");
}