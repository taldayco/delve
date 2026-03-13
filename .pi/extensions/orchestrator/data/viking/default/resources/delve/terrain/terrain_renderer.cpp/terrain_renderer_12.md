if (!dst_ptr) {
    // UploadManager overflow — fall back to a one-shot transfer buffer.
    SDL_GPUTransferBufferCreateInfo ti = {};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size  = byte_size;
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu_device, &ti);
    if (!transfer) { current_light_count = 0; return; }
    void *mapped = SDL_MapGPUTransferBuffer(gpu_device, transfer, false);
    if (!mapped) { SDL_ReleaseGPUTransferBuffer(gpu_device, transfer); current_light_count = 0; return; }
    SDL_memcpy(mapped, lights.data(), byte_size);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { transfer, 0 };
    SDL_GPUBufferRegion           dst_reg = { point_light_ssbo, 0, byte_size };
    SDL_UploadToGPUBuffer(copy, &src, &dst_reg, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer);
  } else {
    SDL_memcpy(dst_ptr, lights.data(), byte_size);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { uploader.buffer, offset };
    SDL_GPUBufferRegion           dst_reg = { point_light_ssbo, 0, byte_size };
    SDL_UploadToGPUBuffer(copy, &src, &dst_reg, false);
    SDL_EndGPUCopyPass(copy);
  }

  current_light_count = count;
  static bool logged_count = false;
  if (!logged_count && count > 0) {
    SDL_Log("TerrainRenderer: Uploaded %u lights", count);
    logged_count = true;
  }
}




void TerrainRenderer::rebuild_clusters_if_needed(SDL_GPUCommandBuffer *cmd,
                                                  uint32_t w, uint32_t h,
                                                  float tile_px, uint32_t num_slices,
                                                  float near_plane, float far_plane) {
  uint32_t tilesX = (uint32_t)std::ceil(w / tile_px);
  uint32_t tilesY = (uint32_t)std::ceil(h / tile_px);

  if (tilesX == cluster_grid_w && tilesY == cluster_grid_y) return;


  init_cluster_buffers(gpu_device, tilesX, tilesY, num_slices);

  if (!cluster_gen_pipeline || !cluster_aabb_ssbo) return;


  struct ClusterGenUniforms {
    float tile_px, grid_size_x, grid_size_y, num_slices;
    float near_plane, far_plane, screen_w, screen_h;
    float _pad0, _pad1;
  } cu;
  cu.tile_px     = tile_px;
  cu.grid_size_x = (float)tilesX;
  cu.grid_size_y = (float)tilesY;
  cu.num_slices  = (float)num_slices;
  cu.near_plane  = near_plane;
  cu.far_plane   = far_plane;
  cu.screen_w    = (float)w;
  cu.screen_h    = (float)h;
  cu._pad0 = cu._pad1 = 0.0f;

  SDL_GPUStorageBufferReadWriteBinding rw_binds[1] = {};
  rw_binds[0].buffer = cluster_aabb_ssbo;


  SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, nullptr, 0, rw_binds, 1);
  SDL_BindGPUComputePipeline(pass, cluster_gen_pipeline);
  SDL_PushGPUComputeUniformData(cmd, 0, &cu, sizeof(cu));



  uint32_t dispX = (tilesX + 15) / 16;
  uint32_t dispY = (tilesY + 8) / 9;
  SDL_DispatchGPUCompute(pass, dispX, dispY, num_slices);
  SDL_EndGPUComputePass(pass);
}







void TerrainRenderer::stage_cull_lights(SDL_GPUCommandBuffer *cmd,
                                         const SceneUniforms &u,
                                         const std::vector<GpuPointLight> &lights) {
  if (!light_culling_pipeline || !cluster_aabb_ssbo || !light_grid_ssbo ||
      !global_index_ssbo || !point_light_ssbo || !cull_counter_ssbo)
    return;







  {

    if (!counter_reset_transfer) {
      SDL_GPUTransferBufferCreateInfo ti = {};
      ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
      ti.size  = sizeof(uint32_t);
      counter_reset_transfer = SDL_CreateGPUTransferBuffer(gpu_device, &ti);
    }

    if (counter_reset_transfer) {
      uint32_t *mapped = (uint32_t *)SDL_MapGPUTransferBuffer(gpu_device, counter_reset_transfer, true);
      if (mapped) {
        *mapped = 0;
        SDL_UnmapGPUTransferBuffer(gpu_device, counter_reset_transfer);

        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src = { counter_reset_transfer, 0 };
        SDL_GPUBufferRegion           dst = { cull_counter_ssbo, 0, sizeof(uint32_t) };
        SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        SDL_EndGPUCopyPass(copy);
      }
    }
  }