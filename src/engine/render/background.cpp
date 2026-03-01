#include "background.h"
#include <SDL3/SDL.h>
#include <string>

#ifdef SHADER_DIR
static const std::string s_shader_dir = SHADER_DIR;
#else
static const std::string s_shader_dir = "shaders";
#endif

bool BackgroundRenderer::build_pipeline(SDL_GPUTextureFormat swapchain_format,
                                         SDL_GPUTextureFormat depth_format) {
    SDL_GPUShader *vert = asset_manager->load_shader(
        "background.vert",
        s_shader_dir + "/background.vert.glsl.spv",
        SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);

    SDL_GPUShader *frag = asset_manager->load_shader(
        "background.frag",
        s_shader_dir + "/background.frag.glsl.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

    if (!vert || !frag) return false;

    SDL_GPUColorTargetDescription color_desc = {};
    color_desc.format = swapchain_format;

    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader   = vert;
    pi.fragment_shader = frag;
    pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions = &color_desc;
    pi.target_info.num_color_targets         = 1;
    pi.target_info.has_depth_stencil_target  = true;
    pi.target_info.depth_stencil_format      = depth_format;
    pi.depth_stencil_state.enable_depth_test  = false;
    pi.depth_stencil_state.enable_depth_write = false;

    if (pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        pipeline = nullptr;
    }

    pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
    if (!pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "BackgroundRenderer: Failed to create pipeline: %s", SDL_GetError());
        return false;
    }

    SDL_Log("BackgroundRenderer: Pipeline created successfully");
    return true;
}

bool BackgroundRenderer::init(SDL_GPUDevice *dev,
                               SDL_GPUTextureFormat swapchain_format,
                               SDL_GPUTextureFormat depth_format,
                               AssetManager &am) {
    device        = dev;
    asset_manager = &am;

    asset_manager->register_pipeline("background", "background.vert", "background.frag");
    return build_pipeline(swapchain_format, depth_format);
}

void BackgroundRenderer::rebuild_if_dirty(SDL_GPUTextureFormat swapchain_format,
                                           SDL_GPUTextureFormat depth_format) {
    if (!asset_manager || !asset_manager->pipeline_needs_rebuild("background")) return;
    build_pipeline(swapchain_format, depth_format);
    asset_manager->clear_rebuild_flag("background");
}

void BackgroundRenderer::draw(SDL_GPUCommandBuffer *cmd,
                               SDL_GPURenderPass   *render_pass,
                               float                time,
                               float                cam_x,
                               float                cam_y) {
    if (!pipeline || !render_pass) return;

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);

    struct { float time, cam_x, cam_y, pad; } u = { time, cam_x, cam_y, 0.0f };
    SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));

    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
}

void BackgroundRenderer::cleanup() {
    if (pipeline && device) {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        pipeline = nullptr;
    }
}
