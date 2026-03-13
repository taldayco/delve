SDL_GPUVertexAttribute attrs[1] = {};
      attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 };

      SDL_GPUColorTargetDescription color_desc = {};
      color_desc.format = swapchain_format;
      color_desc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
      color_desc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      color_desc.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
      color_desc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
      color_desc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      color_desc.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
      color_desc.blend_state.enable_blend          = true;

      SDL_GPUGraphicsPipelineCreateInfo pi = {};
      pi.vertex_shader   = vert;
      pi.fragment_shader = frag;
      pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc;
      pi.vertex_input_state.num_vertex_buffers         = 1;
      pi.vertex_input_state.vertex_attributes          = attrs;
      pi.vertex_input_state.num_vertex_attributes      = 1;
      pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_LINELIST;
      pi.target_info.color_target_descriptions         = &color_desc;
      pi.target_info.num_color_targets                 = 1;
      pi.target_info.has_depth_stencil_target          = true;
      pi.target_info.depth_stencil_format              = depth_stencil_format;
      pi.depth_stencil_state.compare_op                = SDL_GPU_COMPAREOP_ALWAYS;
      pi.depth_stencil_state.enable_depth_test         = false;
      pi.depth_stencil_state.enable_depth_write        = false;

      contour_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
      asset_manager->register_pipeline("contour", "contour.vert", "contour.frag");
    }
    // Shaders owned by asset_manager.
  }

  SDL_Log("TerrainRenderer: Graphics pipelines created");
}




void TerrainRenderer::init_compute_pipelines(SDL_GPUDevice *device) {
  std::string shader_dir = SHADER_DIR;
  SDL_Log("TerrainRenderer: Loading compute shaders from %s", shader_dir.c_str());

  std::string gen_path = shader_dir + "/generate_clusters.comp.glsl.spv";
  asset_manager->load_compute_shader("generate_clusters.comp", gen_path, 1, 1, 0);
  asset_manager->register_compute_pipeline("cluster_gen", "generate_clusters.comp");

  SDL_Log("TerrainRenderer: Creating cluster_gen_pipeline from %s", gen_path.c_str());
  cluster_gen_pipeline = build_compute_pipeline(device, gen_path.c_str(), 1, 1, 0);

  std::string cull_path = shader_dir + "/light_culling.comp.glsl.spv";
  asset_manager->load_compute_shader("light_culling.comp", cull_path, 2, 5, 0);
  asset_manager->register_compute_pipeline("light_culling", "light_culling.comp");

  SDL_Log("TerrainRenderer: Creating light_culling_pipeline from %s", cull_path.c_str());
  light_culling_pipeline = build_compute_pipeline(device, cull_path.c_str(), 2, 5, 0);

  if (cluster_gen_pipeline && light_culling_pipeline)
    SDL_Log("TerrainRenderer: Compute pipelines created");
  else
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "TerrainRenderer: Failed to create one or more compute pipelines");
}

void TerrainRenderer::init_instanced_pipeline(SDL_GPUDevice *device, SDL_Window *window) {
  std::string shader_dir = SHADER_DIR;
  SDL_GPUTextureFormat swapchain_format =
      SDL_GetGPUSwapchainTextureFormat(device, window);

  SDL_GPUShader *vert = asset_manager->load_shader(
      "instanced_terrain.vert", shader_dir + "/instanced_terrain.vert.glsl.spv",
      SDL_GPU_SHADERSTAGE_VERTEX, 1, 1);  // 1 UBO, 1 SSBO (instance buffer)
  SDL_GPUShader *frag = asset_manager->load_shader(
      "instanced_terrain.frag", shader_dir + "/instanced_terrain.frag.glsl.spv",
      SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 3);  // 1 UBO, 3 SSBOs

  if (!vert || !frag) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "TerrainRenderer: Instanced terrain shaders not available yet");
    return;
  }