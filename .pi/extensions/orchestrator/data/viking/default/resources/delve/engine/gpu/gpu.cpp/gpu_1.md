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