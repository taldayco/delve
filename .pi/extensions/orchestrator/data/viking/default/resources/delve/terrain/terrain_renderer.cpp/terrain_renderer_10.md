SDL_GPUTransferBufferCreateInfo ti = {};
  ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  ti.size  = total_sz;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &ti);
  if (!transfer) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "TerrainRenderer::upload_mesh: Failed to create transfer buffer (%u bytes): %s",
                 total_sz, SDL_GetError());
    return;
  }

  uint8_t *mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, transfer, false);
  if (!mapped) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "TerrainRenderer::upload_mesh: Failed to map transfer buffer: %s", SDL_GetError());
    return;
  }

  // Copy all sections into the staging buffer.
  if (basalt_vbo_sz)  SDL_memcpy(mapped + off_basalt_vbo,  all_verts.data(),               basalt_vbo_sz);
  if (basalt_ibo_sz)  SDL_memcpy(mapped + off_basalt_ibo,  all_indices.data(),              basalt_ibo_sz);
  if (lava_vbo_sz)    SDL_memcpy(mapped + off_lava_vbo,    mesh.lava_vertices.data(),       lava_vbo_sz);
  if (lava_ibo_sz)    SDL_memcpy(mapped + off_lava_ibo,    mesh.lava_indices.data(),        lava_ibo_sz);
  if (contour_vbo_sz) SDL_memcpy(mapped + off_contour_vbo, mesh.contour_vertices.data(),    contour_vbo_sz);

  SDL_UnmapGPUTransferBuffer(device, transfer);

  // --- Create all GPU buffers ---

  if (basalt_vbo_sz && basalt_ibo_sz) {
    basalt_vbo = gpu_create_buffer(device, basalt_vbo_sz,  SDL_GPU_BUFFERUSAGE_VERTEX);
    basalt_ibo = gpu_create_buffer(device, basalt_ibo_sz,  SDL_GPU_BUFFERUSAGE_INDEX);
  }
  if (lava_vbo_sz) {
    lava_vbo          = gpu_create_buffer(device, lava_vbo_sz,    SDL_GPU_BUFFERUSAGE_VERTEX);
    lava_vertex_count = (uint32_t)mesh.lava_vertices.size();
  }
  if (lava_ibo_sz) {
    lava_ibo          = gpu_create_buffer(device, lava_ibo_sz,    SDL_GPU_BUFFERUSAGE_INDEX);
    lava_index_count  = (uint32_t)mesh.lava_indices.size();
  }
  if (contour_vbo_sz) {
    contour_vbo          = gpu_create_buffer(device, contour_vbo_sz, SDL_GPU_BUFFERUSAGE_VERTEX);
    contour_vertex_count = (uint32_t)mesh.contour_vertices.size();
  }

  // --- One command buffer, one copy pass, all uploads ---

  SDL_GPUCommandBuffer *cmd  = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass      *copy = SDL_BeginGPUCopyPass(cmd);

  auto upload = [&](SDL_GPUBuffer *buf, uint32_t offset, uint32_t size) {
    if (!buf || size == 0) return;
    SDL_GPUTransferBufferLocation src = { transfer, offset };
    SDL_GPUBufferRegion           dst = { buf, 0, size };
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
  };

  upload(basalt_vbo,  off_basalt_vbo,  basalt_vbo_sz);
  upload(basalt_ibo,  off_basalt_ibo,  basalt_ibo_sz);
  upload(lava_vbo,    off_lava_vbo,    lava_vbo_sz);
  upload(lava_ibo,    off_lava_ibo,    lava_ibo_sz);
  upload(contour_vbo, off_contour_vbo, contour_vbo_sz);

  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);

  // One final wait so the transfer buffer is safe to release.
  SDL_WaitForGPUIdle(device);
  SDL_ReleaseGPUTransferBuffer(device, transfer);

  // --- Register buffers with asset manager ---

  if (asset_manager) {
    if (basalt_vbo)  asset_manager->register_buffer("basalt_vbo",  basalt_vbo);
    if (basalt_ibo)  asset_manager->register_buffer("basalt_ibo",  basalt_ibo);
    if (lava_vbo)    asset_manager->register_buffer("lava_vbo",    lava_vbo);
    if (lava_ibo)    asset_manager->register_buffer("lava_ibo",    lava_ibo);
    if (contour_vbo) asset_manager->register_buffer("contour_vbo", contour_vbo);
  }

  has_data = true;
  SDL_Log("TerrainRenderer: Mesh uploaded (basalt=%u idx, lava=%u verts/%u idx, contour=%u verts) staging=%u bytes",
          basalt_total_index_count, lava_vertex_count, lava_index_count,
          contour_vertex_count, total_sz);
}