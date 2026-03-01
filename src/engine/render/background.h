#pragma once
#include <SDL3/SDL_gpu.h>
#include "core/asset_manager.h"

class BackgroundRenderer {
public:
    bool init(SDL_GPUDevice       *device,
              SDL_GPUTextureFormat swapchain_format,
              SDL_GPUTextureFormat depth_format,
              AssetManager        &am);

    void draw(SDL_GPUCommandBuffer *cmd,
              SDL_GPURenderPass    *render_pass,
              float                 time,
              float                 cam_x,
              float                 cam_y);

    void rebuild_if_dirty(SDL_GPUTextureFormat swapchain_format,
                          SDL_GPUTextureFormat depth_format);

    void cleanup();

private:
    SDL_GPUDevice            *device          = nullptr;
    SDL_GPUGraphicsPipeline  *pipeline        = nullptr;
    AssetManager             *asset_manager   = nullptr;

    bool build_pipeline(SDL_GPUTextureFormat swapchain_format,
                        SDL_GPUTextureFormat depth_format);
};
