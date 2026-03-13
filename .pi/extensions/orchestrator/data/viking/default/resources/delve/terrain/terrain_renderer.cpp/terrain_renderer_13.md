struct CullUniforms {
    float tile_px, grid_size_x, grid_size_y, num_slices;
    float near_plane, far_plane, screen_w, screen_h;
    float light_count_f, _pad0, _pad1, _pad2;
  } cu;
  static_assert(sizeof(CullUniforms) == 48, "CullUniforms must be 48 bytes");
  cu.tile_px       = u.tile_px;
  cu.grid_size_x   = u.grid_size_x;
  cu.grid_size_y   = u.grid_size_y;
  cu.num_slices    = u.num_slices;
  cu.near_plane    = u.near_plane;
  cu.far_plane     = u.far_plane;
  cu.screen_w      = u.grid_size_x * u.tile_px;
  cu.screen_h      = u.grid_size_y * u.tile_px;
  cu.light_count_f = (float)current_light_count;
  cu._pad0 = cu._pad1 = cu._pad2 = 0.0f;
  glm::mat4 view_proj = u.projection * u.view;



  SDL_GPUStorageBufferReadWriteBinding rw[5] = {};
  rw[0].buffer = point_light_ssbo;
  rw[1].buffer = cluster_aabb_ssbo;
  rw[2].buffer = light_grid_ssbo;
  rw[3].buffer = global_index_ssbo;
  rw[4].buffer = cull_counter_ssbo;

  SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, nullptr, 0, rw, 5);
  SDL_BindGPUComputePipeline(pass, light_culling_pipeline);
  SDL_PushGPUComputeUniformData(cmd, 0, &cu, sizeof(cu));
  SDL_PushGPUComputeUniformData(cmd, 1, &view_proj, sizeof(view_proj));


  uint32_t dispX = (cluster_grid_w + 15) / 16;
  uint32_t dispY = (cluster_grid_y + 8) / 9;
  uint32_t dispZ = 24;
  SDL_DispatchGPUCompute(pass, dispX, dispY, dispZ);
  SDL_EndGPUComputePass(pass);
}




void TerrainRenderer::stage_shaded_draw(SDL_GPURenderPass *pass,
                                         SDL_GPUCommandBuffer *cmd,
                                         const SceneUniforms &uniforms) {

  if (use_instanced && instanced_terrain_pipeline && instanced_terrain && instanced_terrain->has_data()
      && gltf_column_vbo && gltf_column_ibo && gltf_column_index_count > 0) {
    stage_instanced_draw(pass, cmd, uniforms);
  } else if (basalt_vbo && basalt_ibo && basalt_total_index_count > 0 && terrain_pipeline) {
    // Select pipeline: PBR or Lambertian
    SDL_GPUGraphicsPipeline *active_pipeline =
        (use_pbr && pbr_terrain_pipeline) ? pbr_terrain_pipeline : terrain_pipeline;
    SDL_BindGPUGraphicsPipeline(pass, active_pipeline);
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

    {
      SDL_GPUBuffer *frag_storage[3] = {
        point_light_ssbo  ? point_light_ssbo  : dummy_ssbo,
        light_grid_ssbo   ? light_grid_ssbo   : dummy_ssbo,
        global_index_ssbo ? global_index_ssbo : dummy_ssbo,
      };
      SDL_BindGPUFragmentStorageBuffers(pass, 0, frag_storage, 3);
    }

    SDL_GPUBufferBinding vbind = { basalt_vbo, 0 };
    SDL_GPUBufferBinding ibind = { basalt_ibo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
    SDL_BindGPUIndexBuffer(pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(pass, basalt_total_index_count, 1, 0, 0, 0);
  }


  if (lava_vbo && lava_ibo && lava_index_count > 0 && lava_pipeline) {
    SDL_BindGPUGraphicsPipeline(pass, lava_pipeline);
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
    SDL_GPUBufferBinding vbind = { lava_vbo, 0 };
    SDL_GPUBufferBinding ibind = { lava_ibo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
    SDL_BindGPUIndexBuffer(pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(pass, lava_index_count, 1, 0, 0, 0);
  }


  if (contour_vbo && contour_vertex_count > 0 && contour_pipeline) {
    SDL_BindGPUGraphicsPipeline(pass, contour_pipeline);
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
    SDL_GPUBufferBinding vbind = { contour_vbo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
    SDL_DrawGPUPrimitives(pass, contour_vertex_count, 1, 0, 0);
  }
}




void TerrainRenderer::draw(SDL_GPUCommandBuffer *cmd,
                            SDL_GPUTexture *swapchain,
                            uint32_t w, uint32_t h,
                            const SceneUniforms &uniforms,
                            const std::vector<GpuPointLight> &lights,
                            UploadManager &uploader) {
  if (!initialized || !has_data) return;