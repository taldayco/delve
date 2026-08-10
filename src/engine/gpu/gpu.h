#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

struct UploadManager {
  SDL_GPUTransferBuffer *buffer   = nullptr;
  uint8_t               *mapped   = nullptr;
  uint32_t               capacity = 0;
  uint32_t               cursor   = 0;

  void init(SDL_GPUDevice *device, uint32_t size);
  void cleanup(SDL_GPUDevice *device);
  void *alloc(uint32_t size, uint32_t *out_offset);
  void  reset();
};

struct GpuContext {
  SDL_Window    *window         = nullptr;
  SDL_Window    *game_window    = nullptr;
  SDL_GPUDevice *device         = nullptr;
  UploadManager  upload_manager;
};

struct FrameContext {
  SDL_GPUCommandBuffer *cmd          = nullptr;
  SDL_GPUTexture       *swapchain    = nullptr;
  SDL_GPURenderPass    *render_pass  = nullptr;
  uint32_t              swapchain_w  = 0;
  uint32_t              swapchain_h  = 0;
};

bool gpu_init(GpuContext &ctx);
bool gpu_create_game_window(GpuContext &ctx);
void gpu_destroy_game_window(GpuContext &ctx);
bool gpu_acquire_frame(GpuContext &ctx, FrameContext &frame);
bool gpu_acquire_game_frame(GpuContext &ctx, FrameContext &frame);
bool gpu_begin_render_pass(GpuContext &ctx, FrameContext &frame);
void gpu_end_frame(FrameContext &frame);
void gpu_cleanup(GpuContext &ctx);


SDL_GPUBuffer *gpu_create_buffer(SDL_GPUDevice *device, uint32_t size,
                                  SDL_GPUBufferUsageFlags usage);
SDL_GPUBuffer *gpu_upload_buffer(SDL_GPUDevice *device, const void *data,
                                  uint32_t size, SDL_GPUBufferUsageFlags usage);
SDL_GPUBuffer *gpu_create_zeroed_buffer(SDL_GPUDevice *device, uint32_t size,
                                         SDL_GPUBufferUsageFlags usage);
SDL_GPUTexture *gpu_upload_texture_rg8(SDL_GPUDevice *device, const uint8_t *rg,
                                        uint32_t w, uint32_t h);
SDL_GPUSampler *gpu_create_linear_clamp_sampler(SDL_GPUDevice *device);
