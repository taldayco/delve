frame.render_pass = SDL_BeginGPURenderPass(frame.cmd, &color_target, 1, nullptr);
  return frame.render_pass != nullptr;
}

void gpu_end_frame(FrameContext &frame) {
  if (frame.render_pass) SDL_EndGPURenderPass(frame.render_pass);
  SDL_SubmitGPUCommandBuffer(frame.cmd);
}

void gpu_cleanup(GpuContext &ctx) {
  SDL_WaitForGPUIdle(ctx.device);
  ctx.upload_manager.cleanup(ctx.device);
  if (ctx.game_window) {
    SDL_ReleaseWindowFromGPUDevice(ctx.device, ctx.game_window);
    SDL_DestroyWindow(ctx.game_window);
  }
  if (ctx.device)  SDL_DestroyGPUDevice(ctx.device);
  if (ctx.window)  SDL_DestroyWindow(ctx.window);
  SDL_Quit();
}

void release_texture(SDL_GPUDevice *device, const TextureHandle &handle) {
  if (handle.sampler) SDL_ReleaseGPUSampler(device, handle.sampler);
  if (handle.texture) SDL_ReleaseGPUTexture(device, handle.texture);
}

TextureHandle upload_pixels_to_texture(SDL_GPUDevice *device,
                                       const uint32_t *pixels, int width,
                                       int height) {
  SDL_GPUTextureCreateInfo tex_info = {};
  tex_info.type                 = SDL_GPU_TEXTURETYPE_2D;
  tex_info.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tex_info.width                = (uint32_t)width;
  tex_info.height               = (uint32_t)height;
  tex_info.layer_count_or_depth = 1;
  tex_info.num_levels           = 1;
  tex_info.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;

  SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &tex_info);
  if (!texture) return {};

  SDL_GPUTransferBufferCreateInfo ti = {};
  ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  ti.size  = (uint32_t)(width * height * 4);
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &ti);
  if (!transfer) { SDL_ReleaseGPUTexture(device, texture); return {}; }

  void *data = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (!data) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, texture);
    return {};
  }
  SDL_memcpy(data, pixels, ti.size);
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer *cmd   = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass      *pass  = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo src = { transfer, 0 };
  SDL_GPUTextureRegion       dst = { texture, 0, 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };
  SDL_UploadToGPUTexture(pass, &src, &dst, false);
  SDL_EndGPUCopyPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_WaitForGPUIdle(device);
  SDL_ReleaseGPUTransferBuffer(device, transfer);

  SDL_GPUSamplerCreateInfo si = {};
  si.min_filter        = SDL_GPU_FILTER_LINEAR;
  si.mag_filter        = SDL_GPU_FILTER_LINEAR;
  si.mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  si.address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  si.address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  si.address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &si);
  if (!sampler) { SDL_ReleaseGPUTexture(device, texture); return {}; }

  return { texture, sampler, width, height };
}

TextureHandle upload_rgba_texture(SDL_GPUDevice *device,
                                  const uint8_t *pixels, int width,
                                  int height, bool srgb) {
  SDL_GPUTextureCreateInfo tex_info = {};
  tex_info.type                 = SDL_GPU_TEXTURETYPE_2D;
  tex_info.format               = srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
                                       : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tex_info.width                = (uint32_t)width;
  tex_info.height               = (uint32_t)height;
  tex_info.layer_count_or_depth = 1;
  tex_info.num_levels           = 1;
  tex_info.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;

  SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &tex_info);
  if (!texture) return {};