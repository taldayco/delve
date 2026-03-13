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
  si.address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  si.address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  si.address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &si);
  if (!sampler) { SDL_ReleaseGPUTexture(device, texture); return {}; }

  return { texture, sampler, width, height };
}

void UploadManager::init(SDL_GPUDevice *device, uint32_t size) {
  capacity = size;
  cursor   = 0;
  SDL_GPUTransferBufferCreateInfo ti = {};
  ti.usage  = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  ti.size   = size;
  buffer = SDL_CreateGPUTransferBuffer(device, &ti);
  if (!buffer) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "UploadManager::init: Failed to create transfer buffer: %s", SDL_GetError());
    capacity = 0;
    return;
  }
  // Map persistently — SDL3-GPU allows the buffer to stay mapped between frames.
  mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, buffer, false);
  if (!mapped) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "UploadManager::init: Failed to map transfer buffer: %s", SDL_GetError());
  }
}

void UploadManager::cleanup(SDL_GPUDevice *device) {
  if (buffer) {
    SDL_UnmapGPUTransferBuffer(device, buffer);
    SDL_ReleaseGPUTransferBuffer(device, buffer);
    buffer   = nullptr;
    mapped   = nullptr;
    capacity = 0;
    cursor   = 0;
  }
}

void *UploadManager::alloc(uint32_t size, uint32_t *out_offset) {
  // 256-byte align for GPU safety
  uint32_t aligned_cursor = (cursor + 255u) & ~255u;
  if (!mapped || aligned_cursor + size > capacity) return nullptr;
  *out_offset = aligned_cursor;
  cursor      = aligned_cursor + size;
  return mapped + aligned_cursor;
}

void UploadManager::reset() {
  cursor = 0;
}

SDL_GPUBuffer *gpu_create_buffer(SDL_GPUDevice *device, uint32_t size,
                                  SDL_GPUBufferUsageFlags usage) {
  SDL_GPUBufferCreateInfo info = {};
  info.usage = usage;
  info.size  = size;
  SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(device, &info);
  if (!buf)
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "gpu_create_buffer: Failed (size=%u): %s", size, SDL_GetError());
  return buf;
}

SDL_GPUBuffer *gpu_upload_buffer(SDL_GPUDevice *device, const void *data,
                                  uint32_t size, SDL_GPUBufferUsageFlags usage) {
  SDL_GPUBuffer *buffer = gpu_create_buffer(device, size, usage);
  if (!buffer) return nullptr;

  SDL_GPUTransferBufferCreateInfo ti = {};
  ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  ti.size  = size;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &ti);
  if (!transfer) { SDL_ReleaseGPUBuffer(device, buffer); return nullptr; }