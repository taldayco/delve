rebuild_graphics("pbr_terrain", pbr_terrain_pipeline, [&]() -> SDL_GPUGraphicsPipeline * {
    SDL_GPUShader *vert = asset_manager->load_shader("pbr_static.vert", shader_dir + "/pbr_static.vert.glsl.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *frag = asset_manager->load_shader("pbr_static.frag", shader_dir + "/pbr_static.frag.glsl.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 3);
    if (!vert || !frag) return nullptr;
    SDL_GPUVertexBufferDescription vbuf_desc = {};
    vbuf_desc.slot = 0; vbuf_desc.pitch = sizeof(BasaltVertex); vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[4] = {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, pos_x)   };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, color_r) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(BasaltVertex, sheen)   };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, nx)      };
    SDL_GPUColorTargetDescription cd = {}; cd.format = swapchain_format;
    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader = vert; pi.fragment_shader = frag;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc; pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = attrs; pi.vertex_input_state.num_vertex_attributes = 4;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions = &cd; pi.target_info.num_color_targets = 1;
    pi.target_info.has_depth_stencil_target = true; pi.target_info.depth_stencil_format = depth_stencil_format;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pi.depth_stencil_state.enable_depth_test = true; pi.depth_stencil_state.enable_depth_write = true;
    return SDL_CreateGPUGraphicsPipeline(gpu_device, &pi);
  });

  // Compute pipelines
  if (asset_manager->pipeline_needs_rebuild("cluster_gen")) {
    SDL_WaitForGPUIdle(gpu_device);
    if (cluster_gen_pipeline) { SDL_ReleaseGPUComputePipeline(gpu_device, cluster_gen_pipeline); cluster_gen_pipeline = nullptr; }
    std::string gen_path = shader_dir + "/generate_clusters.comp.glsl.spv";
    cluster_gen_pipeline = build_compute_pipeline(gpu_device, gen_path.c_str(), 1, 1, 0);
    asset_manager->clear_rebuild_flag("cluster_gen");
    SDL_Log("TerrainRenderer: Rebuilt pipeline 'cluster_gen'");
  }
  if (asset_manager->pipeline_needs_rebuild("light_culling")) {
    SDL_WaitForGPUIdle(gpu_device);
    if (light_culling_pipeline) { SDL_ReleaseGPUComputePipeline(gpu_device, light_culling_pipeline); light_culling_pipeline = nullptr; }
    std::string cull_path = shader_dir + "/light_culling.comp.glsl.spv";
    light_culling_pipeline = build_compute_pipeline(gpu_device, cull_path.c_str(), 2, 5, 0);
    asset_manager->clear_rebuild_flag("light_culling");
    SDL_Log("TerrainRenderer: Rebuilt pipeline 'light_culling'");
  }
}

void TerrainRenderer::init_cluster_buffers(SDL_GPUDevice *device,
                                            uint32_t tilesX, uint32_t tilesY,
                                            uint32_t num_slices) {
  release_cluster_buffers(device);

  uint32_t num_clusters = tilesX * tilesY * num_slices;


  cluster_aabb_ssbo = gpu_create_buffer(
      device,
      num_clusters * 32,
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);



  light_grid_ssbo = gpu_create_zeroed_buffer(
      device,
      num_clusters * 8,
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
      SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);