#include "frame_capture.h"
#include <SDL3/SDL.h>
#include <vector>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

bool capture_frame_to_png(SDL_GPUDevice *device, SDL_Window *window, const std::string &output_path) {
  if (!device || !window) return false;

  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  if (w <= 0 || h <= 0) return false;

  // Create a texture to copy the swapchain into
  SDL_GPUTextureCreateInfo tex_info{};
  tex_info.type = SDL_GPU_TEXTURETYPE_2D;
  tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  tex_info.width = (uint32_t)w;
  tex_info.height = (uint32_t)h;
  tex_info.layer_count_or_depth = 1;
  tex_info.num_levels = 1;
  tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

  SDL_GPUTexture *readback_tex = SDL_CreateGPUTexture(device, &tex_info);
  if (!readback_tex) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "frame_capture: failed to create readback texture");
    return false;
  }

  // Create transfer buffer for CPU readback
  uint32_t row_pitch = (uint32_t)w * 4;
  uint32_t buffer_size = row_pitch * (uint32_t)h;

  SDL_GPUTransferBufferCreateInfo buf_info{};
  buf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  buf_info.size = buffer_size;

  SDL_GPUTransferBuffer *transfer_buf = SDL_CreateGPUTransferBuffer(device, &buf_info);
  if (!transfer_buf) {
    SDL_ReleaseGPUTexture(device, readback_tex);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "frame_capture: failed to create transfer buffer");
    return false;
  }

  // Copy swapchain to our texture, then texture to transfer buffer
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  if (!cmd) {
    SDL_ReleaseGPUTransferBuffer(device, transfer_buf);
    SDL_ReleaseGPUTexture(device, readback_tex);
    return false;
  }

  SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);

  SDL_GPUTextureRegion src_region{};
  src_region.texture = readback_tex;
  src_region.w = (uint32_t)w;
  src_region.h = (uint32_t)h;
  src_region.d = 1;

  SDL_GPUTextureTransferInfo dst_info{};
  dst_info.transfer_buffer = transfer_buf;
  dst_info.offset = 0;
  dst_info.pixels_per_row = (uint32_t)w;
  dst_info.rows_per_layer = (uint32_t)h;

  SDL_DownloadFromGPUTexture(copy_pass, &src_region, &dst_info);
  SDL_EndGPUCopyPass(copy_pass);

  SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  SDL_WaitForGPUFences(device, true, &fence, 1);
  SDL_ReleaseGPUFence(device, fence);

  // Map transfer buffer and write PNG
  void *mapped = SDL_MapGPUTransferBuffer(device, transfer_buf, false);
  bool success = false;
  if (mapped) {
    success = stbi_write_png(output_path.c_str(), w, h, 4, mapped, (int)row_pitch) != 0;
    SDL_UnmapGPUTransferBuffer(device, transfer_buf);
  }

  SDL_ReleaseGPUTransferBuffer(device, transfer_buf);
  SDL_ReleaseGPUTexture(device, readback_tex);

  if (success) {
    SDL_Log("frame_capture: saved %dx%d frame to %s", w, h, output_path.c_str());
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "frame_capture: failed to write PNG");
  }

  return success;
}
