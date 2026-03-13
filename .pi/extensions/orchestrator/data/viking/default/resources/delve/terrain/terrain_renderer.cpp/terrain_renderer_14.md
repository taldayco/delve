upload_lights(cmd, uploader, lights);
  stage_cull_lights(cmd, uniforms, lights);

  SDL_GPURenderPass *pass = begin_render_pass_load(cmd, swapchain, w, h);
  if (!pass) return;
  stage_shaded_draw(pass, cmd, uniforms);
  SDL_EndGPURenderPass(pass);
}




SDL_GPURenderPass *TerrainRenderer::begin_render_pass(SDL_GPUCommandBuffer *cmd,
                                                       SDL_GPUTexture *swapchain,
                                                       uint32_t w, uint32_t h) {

  desired_depth_w = w;
  desired_depth_h = h;
  if (!depth_texture || depth_w != w || depth_h != h) return nullptr;

  SDL_GPUColorTargetInfo color_target = {};
  color_target.texture     = swapchain;
  color_target.load_op     = SDL_GPU_LOADOP_CLEAR;
  color_target.store_op    = SDL_GPU_STOREOP_STORE;
  color_target.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
  color_target.cycle       = false;

  SDL_GPUDepthStencilTargetInfo depth_target = {};
  depth_target.texture          = depth_texture;
  depth_target.load_op          = SDL_GPU_LOADOP_CLEAR;
  depth_target.store_op         = SDL_GPU_STOREOP_STORE;
  depth_target.clear_depth      = 1.0f;
  depth_target.stencil_load_op  = SDL_GPU_LOADOP_CLEAR;
  depth_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
  depth_target.clear_stencil    = 0;
  depth_target.cycle            = false;

  return SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
}




SDL_GPURenderPass *TerrainRenderer::begin_render_pass_load(SDL_GPUCommandBuffer *cmd,
                                                            SDL_GPUTexture *swapchain,
                                                            uint32_t w, uint32_t h) {

  desired_depth_w = w;
  desired_depth_h = h;
  if (!depth_texture || depth_w != w || depth_h != h) return nullptr;

  SDL_GPUColorTargetInfo color_target = {};
  color_target.texture   = swapchain;
  color_target.load_op   = SDL_GPU_LOADOP_LOAD;
  color_target.store_op  = SDL_GPU_STOREOP_STORE;
  color_target.cycle     = false;


  SDL_GPUDepthStencilTargetInfo depth_target = {};
  depth_target.texture          = depth_texture;
  depth_target.load_op          = SDL_GPU_LOADOP_CLEAR;
  depth_target.store_op         = SDL_GPU_STOREOP_STORE;
  depth_target.clear_depth      = 1.0f;
  depth_target.stencil_load_op  = SDL_GPU_LOADOP_CLEAR;
  depth_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
  depth_target.clear_stencil    = 0;
  depth_target.cycle            = false;

  return SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
}



SDL_GPURenderPass *TerrainRenderer::begin_render_pass_load_preserve_depth(
    SDL_GPUCommandBuffer *cmd,
    SDL_GPUTexture *swapchain,
    uint32_t w, uint32_t h) {

  desired_depth_w = w;
  desired_depth_h = h;
  if (!depth_texture || depth_w != w || depth_h != h) return nullptr;

  SDL_GPUColorTargetInfo color_target = {};
  color_target.texture  = swapchain;
  color_target.load_op  = SDL_GPU_LOADOP_LOAD;
  color_target.store_op = SDL_GPU_STOREOP_STORE;
  color_target.cycle    = false;

  SDL_GPUDepthStencilTargetInfo depth_target = {};
  depth_target.texture          = depth_texture;
  depth_target.load_op          = SDL_GPU_LOADOP_LOAD;
  depth_target.store_op         = SDL_GPU_STOREOP_STORE;
  depth_target.stencil_load_op  = SDL_GPU_LOADOP_LOAD;
  depth_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
  depth_target.cycle            = false;

  return SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
}



void TerrainRenderer::prepare_frame_resources(SDL_GPUDevice *device) {
  if (desired_depth_w == 0 || desired_depth_h == 0) return;
  if (depth_texture && depth_w == desired_depth_w && depth_h == desired_depth_h) return;

  // Caller (on_pre_frame_game) has already called SDL_WaitForGPUIdle.
  if (depth_texture) {
    SDL_ReleaseGPUTexture(device, depth_texture);
    depth_texture = nullptr;
  }

  SDL_GPUTextureCreateInfo ti = {};
  ti.type                 = SDL_GPU_TEXTURETYPE_2D;
  ti.format               = depth_stencil_format;
  ti.width                = desired_depth_w;
  ti.height               = desired_depth_h;
  ti.layer_count_or_depth = 1;
  ti.num_levels           = 1;
  ti.usage                = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  depth_texture           = SDL_CreateGPUTexture(device, &ti);
  depth_w = desired_depth_w;
  depth_h = desired_depth_h;