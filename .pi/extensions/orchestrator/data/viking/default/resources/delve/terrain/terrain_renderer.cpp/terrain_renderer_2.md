SDL_GPUVertexAttribute attrs[4] = {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, pos_x)   };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, color_r) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(BasaltVertex, sheen)   };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, nx)      };

    SDL_GPUColorTargetDescription color_desc = {};
    color_desc.format = swapchain_format;

    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader   = vert;
    pi.fragment_shader = frag;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc;
    pi.vertex_input_state.num_vertex_buffers         = 1;
    pi.vertex_input_state.vertex_attributes          = attrs;
    pi.vertex_input_state.num_vertex_attributes      = 4;
    pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions         = &color_desc;
    pi.target_info.num_color_targets                 = 1;
    pi.target_info.has_depth_stencil_target          = true;
    pi.target_info.depth_stencil_format              = depth_stencil_format;
    pi.depth_stencil_state.compare_op                = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pi.depth_stencil_state.enable_depth_test         = true;
    pi.depth_stencil_state.enable_depth_write        = true;

    terrain_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);

    asset_manager->register_pipeline("terrain", "terrain.vert", "terrain.frag");
    // Shaders are owned by the asset manager; do NOT release them here.
  }


  {
    SDL_GPUShader *vert = asset_manager->load_shader(
        "lava.vert", shader_dir + "/lava.vert.glsl.spv",
        SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *frag = asset_manager->load_shader(
        "lava.frag", shader_dir + "/lava.frag.glsl.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);

    if (vert && frag) {
      SDL_GPUVertexBufferDescription vbuf_desc = {};
      vbuf_desc.slot       = 0;
      vbuf_desc.pitch      = sizeof(GpuLavaVertex);
      vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

      SDL_GPUVertexAttribute attrs[2] = {};
      attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(GpuLavaVertex, pos_x)       };
      attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(GpuLavaVertex, time_offset) };

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
      pi.depth_stencil_state.compare_op                = SDL_GPU_COMPAREOP_LESS;
      pi.depth_stencil_state.enable_depth_test         = true;
      pi.depth_stencil_state.enable_depth_write        = true;
      pi.depth_stencil_state.enable_stencil_test       = false;

      lava_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
      asset_manager->register_pipeline("lava", "lava.vert", "lava.frag");
    }
    // Shaders owned by asset_manager.
  }


  {
    SDL_GPUShader *vert = asset_manager->load_shader(
        "contour.vert", shader_dir + "/contour.vert.glsl.spv",
        SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *frag = asset_manager->load_shader(
        "contour.frag", shader_dir + "/contour.frag.glsl.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);

    if (vert && frag) {
      SDL_GPUVertexBufferDescription vbuf_desc = {};
      vbuf_desc.slot       = 0;
      vbuf_desc.pitch      = sizeof(ContourVertex);
      vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;