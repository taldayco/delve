void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (!mapped) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUBuffer(device, buffer);
    return nullptr;
  }
  SDL_memcpy(mapped, data, size);
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer *cmd  = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass      *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation src = { transfer, 0 };
  SDL_GPUBufferRegion           dst = { buffer,   0, size };
  SDL_UploadToGPUBuffer(copy, &src, &dst, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_WaitForGPUIdle(device);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  return buffer;
}

SDL_GPUBuffer *gpu_create_zeroed_buffer(SDL_GPUDevice *device, uint32_t size,
                                         SDL_GPUBufferUsageFlags usage) {
  std::vector<uint8_t> zeros(size, 0);
  return gpu_upload_buffer(device, zeros.data(), size, usage);
}