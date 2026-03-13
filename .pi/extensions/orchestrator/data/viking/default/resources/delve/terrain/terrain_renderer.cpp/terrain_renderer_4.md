// GltfVertex layout: position(vec3) + normal(vec3) — only first 2 attrs used
  SDL_GPUVertexBufferDescription vbuf_desc = {};
  vbuf_desc.slot       = 0;
  vbuf_desc.pitch      = 48;  // sizeof(GltfVertex)
  vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

  SDL_GPUVertexAttribute attrs[2] = {};
  attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0  };   // position
  attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12 };   // normal

  SDL_GPUColorTargetDescription color_desc = {};
  color_desc.format = swapchain_format;

  SDL_GPUGraphicsPipelineCreateInfo pi = {};
  pi.vertex_shader   = vert;
  pi.fragment_shader = frag;
  pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc;
  pi.vertex_input_state.num_vertex_buffers         = 1;
  pi.vertex_input_state.vertex_attributes          = attrs;
  pi.vertex_input_state.num_vertex_attributes      = 2;
  pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  pi.target_info.color_target_descriptions         = &color_desc;
  pi.target_info.num_color_targets                 = 1;
  pi.target_info.has_depth_stencil_target          = true;
  pi.target_info.depth_stencil_format              = depth_stencil_format;
  pi.depth_stencil_state.compare_op                = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  pi.depth_stencil_state.enable_depth_test         = true;
  pi.depth_stencil_state.enable_depth_write        = true;

  instanced_terrain_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
  if (instanced_terrain_pipeline)
    asset_manager->register_pipeline("instanced_terrain", "instanced_terrain.vert", "instanced_terrain.frag");
  SDL_Log("TerrainRenderer: Instanced terrain pipeline %s",
          instanced_terrain_pipeline ? "created" : "FAILED");
}

void TerrainRenderer::init_pbr_pipeline(SDL_GPUDevice *device, SDL_Window *window) {
  std::string shader_dir = SHADER_DIR;
  SDL_GPUTextureFormat swapchain_format =
      SDL_GetGPUSwapchainTextureFormat(device, window);

  SDL_GPUShader *vert = asset_manager->load_shader(
      "pbr_static.vert", shader_dir + "/pbr_static.vert.glsl.spv",
      SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
  SDL_GPUShader *frag = asset_manager->load_shader(
      "pbr_static.frag", shader_dir + "/pbr_static.frag.glsl.spv",
      SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 3);

  if (!vert || !frag) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "TerrainRenderer: PBR shaders not available yet");
    return;
  }

  // Same BasaltVertex layout as terrain_pipeline — drop-in replacement
  SDL_GPUVertexBufferDescription vbuf_desc = {};
  vbuf_desc.slot       = 0;
  vbuf_desc.pitch      = sizeof(BasaltVertex);
  vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

  SDL_GPUVertexAttribute attrs[4] = {};
  attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, pos_x)   };
  attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, color_r) };
  attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(BasaltVertex, sheen)   };
  attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, nx)      };

  SDL_GPUColorTargetDescription color_desc = {};
  color_desc.format = swapchain_format;