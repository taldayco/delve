#include "gpu/gpu.h"
#include <algorithm>
#include <vector>

bool gpu_init(GpuContext &ctx) {
  SDL_Log("Init starting...");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
    return false;
  }

  int tool_w = 450;
  int tool_h = 800;

  SDL_DisplayID display_id = SDL_GetPrimaryDisplay();
  SDL_Rect display_bounds;
  if (SDL_GetDisplayUsableBounds(display_id, &display_bounds))
    tool_h = std::min(tool_h, (int)(display_bounds.h * 0.85f));

  ctx.window = SDL_CreateWindow("Topo — Controls", tool_w, tool_h, SDL_WINDOW_RESIZABLE);
  if (!ctx.window) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
    return false;
  }

  ctx.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
  if (!ctx.device) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUDevice failed: %s", SDL_GetError());
    return false;
  }

  if (!SDL_ClaimWindowForGPUDevice(ctx.device, ctx.window)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
    return false;
  }

  SDL_SetGPUSwapchainParameters(ctx.device, ctx.window,
                                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                SDL_GPU_PRESENTMODE_VSYNC);

  ctx.upload_manager.init(ctx.device, 8 * 1024 * 1024);

  SDL_Log("Init complete");
  return true;
}

bool gpu_create_game_window(GpuContext &ctx) {
  if (ctx.game_window) return true;

  SDL_DisplayID display_id = SDL_GetPrimaryDisplay();
  SDL_Rect display_bounds;
  int win_w = 1024, win_h = 1024;

  if (SDL_GetDisplayUsableBounds(display_id, &display_bounds)) {
    win_h = (int)(display_bounds.h * 0.85f);
    win_w = win_h;
  }

  ctx.game_window = SDL_CreateWindow("Topo — Map", win_w, win_h, SDL_WINDOW_RESIZABLE);
  if (!ctx.game_window) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create game window: %s", SDL_GetError());
    return false;
  }

  if (!SDL_ClaimWindowForGPUDevice(ctx.device, ctx.game_window)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to claim game window: %s", SDL_GetError());
    SDL_DestroyWindow(ctx.game_window);
    ctx.game_window = nullptr;
    return false;
  }

  SDL_SetGPUSwapchainParameters(ctx.device, ctx.game_window,
                                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                SDL_GPU_PRESENTMODE_VSYNC);
  SDL_Log("Game window created (%dx%d)", win_w, win_h);
  return true;
}

void gpu_destroy_game_window(GpuContext &ctx) {
  if (!ctx.game_window) return;
  SDL_WaitForGPUIdle(ctx.device);
  SDL_ReleaseWindowFromGPUDevice(ctx.device, ctx.game_window);
  SDL_DestroyWindow(ctx.game_window);
  ctx.game_window = nullptr;
}

bool gpu_acquire_frame(GpuContext &ctx, FrameContext &frame) {
  frame.cmd = SDL_AcquireGPUCommandBuffer(ctx.device);
  if (!frame.cmd) return false;

  if (!SDL_AcquireGPUSwapchainTexture(frame.cmd, ctx.window,
                                      &frame.swapchain, &frame.swapchain_w,
                                      &frame.swapchain_h) || !frame.swapchain) {
    SDL_SubmitGPUCommandBuffer(frame.cmd);
    return false;
  }
  return true;
}

bool gpu_acquire_game_frame(GpuContext &ctx, FrameContext &frame) {
  if (!ctx.game_window) return false;

  ctx.upload_manager.reset();

  frame.cmd = SDL_AcquireGPUCommandBuffer(ctx.device);
  if (!frame.cmd) return false;

  if (!SDL_AcquireGPUSwapchainTexture(frame.cmd, ctx.game_window,
                                      &frame.swapchain, &frame.swapchain_w,
                                      &frame.swapchain_h) || !frame.swapchain) {
    SDL_SubmitGPUCommandBuffer(frame.cmd);
    return false;
  }
  return true;
}

bool gpu_begin_render_pass(GpuContext &ctx, FrameContext &frame) {
  SDL_GPUColorTargetInfo color_target = {};
  color_target.texture     = frame.swapchain;
  color_target.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
  color_target.load_op     = SDL_GPU_LOADOP_CLEAR;
  color_target.store_op    = SDL_GPU_STOREOP_STORE;

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
