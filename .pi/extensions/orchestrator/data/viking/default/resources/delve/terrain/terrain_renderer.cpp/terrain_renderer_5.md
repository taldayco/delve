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

  pbr_terrain_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
  if (pbr_terrain_pipeline)
    asset_manager->register_pipeline("pbr_terrain", "pbr_static.vert", "pbr_static.frag");
  SDL_Log("TerrainRenderer: PBR terrain pipeline %s",
          pbr_terrain_pipeline ? "created" : "FAILED");
}




void TerrainRenderer::rebuild_dirty_pipelines(SDL_Window *window) {
  if (!asset_manager || !gpu_device) return;

  std::string shader_dir = SHADER_DIR;
  SDL_GPUTextureFormat swapchain_format =
      SDL_GetGPUSwapchainTextureFormat(gpu_device, window);

  auto rebuild_graphics = [&](const std::string &key,
                               SDL_GPUGraphicsPipeline *&pipeline_out,
                               auto pipeline_builder) {
    if (asset_manager->pipeline_needs_rebuild(key)) {
      SDL_WaitForGPUIdle(gpu_device);
      if (pipeline_out) { SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline_out); pipeline_out = nullptr; }
      pipeline_out = pipeline_builder();
      asset_manager->clear_rebuild_flag(key);
      SDL_Log("TerrainRenderer: Rebuilt pipeline '%s'", key.c_str());
    }
  };