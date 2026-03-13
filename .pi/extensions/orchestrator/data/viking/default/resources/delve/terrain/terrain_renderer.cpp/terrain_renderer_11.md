void TerrainRenderer::upload_gltf_column_mesh(SDL_GPUDevice *device,
                                               const void *vertex_data, uint32_t vertex_bytes,
                                               const void *index_data, uint32_t index_bytes,
                                               uint32_t index_count) {
  SDL_WaitForGPUIdle(device);
  if (gltf_column_vbo) { SDL_ReleaseGPUBuffer(device, gltf_column_vbo); gltf_column_vbo = nullptr; }
  if (gltf_column_ibo) { SDL_ReleaseGPUBuffer(device, gltf_column_ibo); gltf_column_ibo = nullptr; }

  gltf_column_vbo = gpu_upload_buffer(device, vertex_data, vertex_bytes, SDL_GPU_BUFFERUSAGE_VERTEX);
  gltf_column_ibo = gpu_upload_buffer(device, index_data, index_bytes, SDL_GPU_BUFFERUSAGE_INDEX);
  gltf_column_index_count = index_count;

  SDL_Log("TerrainRenderer: glTF column mesh uploaded (%u indices, VBO=%u bytes, IBO=%u bytes)",
          index_count, vertex_bytes, index_bytes);
}

void TerrainRenderer::stage_instanced_draw(SDL_GPURenderPass *pass,
                                             SDL_GPUCommandBuffer *cmd,
                                             const SceneUniforms &uniforms) {
  if (!instanced_terrain || !instanced_terrain->has_data()) return;
  if (!gltf_column_vbo || !gltf_column_ibo || gltf_column_index_count == 0) return;

  SDL_BindGPUGraphicsPipeline(pass, instanced_terrain_pipeline);
  SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
  SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

  // Vertex storage: instance SSBO at slot 0
  SDL_GPUBuffer *vert_storage[1] = { instanced_terrain->get_instance_ssbo() };
  SDL_BindGPUVertexStorageBuffers(pass, 0, vert_storage, 1);

  // Fragment storage: lights, grid, indices
  SDL_GPUBuffer *frag_storage[3] = {
    point_light_ssbo  ? point_light_ssbo  : dummy_ssbo,
    light_grid_ssbo   ? light_grid_ssbo   : dummy_ssbo,
    global_index_ssbo ? global_index_ssbo : dummy_ssbo,
  };
  SDL_BindGPUFragmentStorageBuffers(pass, 0, frag_storage, 3);

  SDL_GPUBufferBinding vbind = { gltf_column_vbo, 0 };
  SDL_GPUBufferBinding ibind = { gltf_column_ibo, 0 };
  SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
  SDL_BindGPUIndexBuffer(pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
  SDL_DrawGPUIndexedPrimitives(pass, gltf_column_index_count,
                                instanced_terrain->instance_count(), 0, 0, 0);
}

void TerrainRenderer::upload_lights(SDL_GPUCommandBuffer *cmd,
                                     UploadManager &uploader,
                                     const std::vector<GpuPointLight> &lights) {
  if (!point_light_ssbo || lights.empty()) {
    current_light_count = 0;
    return;
  }

  uint32_t count     = (uint32_t)std::min(lights.size(), (size_t)MAX_LIGHTS);
  uint32_t byte_size = count * (uint32_t)sizeof(GpuPointLight);

  uint32_t offset = 0;
  void *dst_ptr   = uploader.alloc(byte_size, &offset);