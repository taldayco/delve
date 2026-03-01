#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

struct TextureHandle {
  SDL_GPUTexture *texture = nullptr;
  SDL_GPUSampler *sampler = nullptr;
  int width = 0;
  int height = 0;
};

// Persistent linear staging allocator — mapped once, reset each frame.
// Allows zero-allocation per-frame uploads into a single transfer buffer.
struct UploadManager {
  SDL_GPUTransferBuffer *buffer   = nullptr;
  uint8_t               *mapped   = nullptr;
  uint32_t               capacity = 0;
  uint32_t               cursor   = 0;

  void init(SDL_GPUDevice *device, uint32_t size);
  void cleanup(SDL_GPUDevice *device);
  // Returns mapped CPU pointer at *out_offset, or nullptr on overflow.
  void *alloc(uint32_t size, uint32_t *out_offset);
  void  reset(); // call at the top of each frame before any alloc()
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

void release_texture(SDL_GPUDevice *device, const TextureHandle &handle);
TextureHandle upload_pixels_to_texture(SDL_GPUDevice *device,
                                       const uint32_t *pixels, int width,
                                       int height);

// GPU buffer utilities (used by terrain renderer and any other subsystem)
SDL_GPUBuffer *gpu_create_buffer(SDL_GPUDevice *device, uint32_t size,
                                  SDL_GPUBufferUsageFlags usage);
SDL_GPUBuffer *gpu_upload_buffer(SDL_GPUDevice *device, const void *data,
                                  uint32_t size, SDL_GPUBufferUsageFlags usage);
SDL_GPUBuffer *gpu_create_zeroed_buffer(SDL_GPUDevice *device, uint32_t size,
                                         SDL_GPUBufferUsageFlags usage);
