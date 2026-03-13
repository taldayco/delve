#include "terrain/terrain_renderer.h"
#include "gpu/gpu.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>





// Helper: build a compute pipeline from SPIR-V on disk (used by init and hot-swap).
static SDL_GPUComputePipeline *build_compute_pipeline(SDL_GPUDevice *device,
                                                       const char *path,
                                                       int num_uniform_buffers,
                                                       int num_rw_storage_buffers,
                                                       int num_ro_storage_buffers = 0) {
  SDL_Log("build_compute_pipeline: Loading %s", path);
  SDL_IOStream *io = SDL_IOFromFile(path, "rb");
  if (!io) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "build_compute_pipeline: Failed to open %s", path);
    return nullptr;
  }
  Sint64 size = SDL_GetIOSize(io);
  if (size <= 0) { SDL_CloseIO(io); return nullptr; }
  std::vector<uint8_t> code(size);
  SDL_ReadIO(io, code.data(), size);
  SDL_CloseIO(io);

  SDL_GPUComputePipelineCreateInfo info = {};
  info.code                          = code.data();
  info.code_size                     = (size_t)size;
  info.entrypoint                    = "main";
  info.format                        = SDL_GPU_SHADERFORMAT_SPIRV;
  info.num_uniform_buffers           = (Uint32)num_uniform_buffers;
  info.num_readwrite_storage_buffers = (Uint32)num_rw_storage_buffers;
  info.num_readonly_storage_buffers  = (Uint32)num_ro_storage_buffers;
  info.threadcount_x                 = 16;
  info.threadcount_y                 = 9;
  info.threadcount_z                 = 1;

  SDL_GPUComputePipeline *pipeline = SDL_CreateGPUComputePipeline(device, &info);
  if (!pipeline)
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "build_compute_pipeline: Failed to create from %s: %s", path, SDL_GetError());
  return pipeline;
}






void TerrainRenderer::init(SDL_GPUDevice *device, SDL_Window *window, AssetManager &am) {
  if (initialized) return;
  if (!device) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainRenderer::init called with NULL device!");
    return;
  }
  gpu_device    = device;
  asset_manager = &am;

  if (SDL_GPUTextureSupportsFormat(device,
          SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
          SDL_GPU_TEXTURETYPE_2D,
          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
    depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
  } else {
    depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
  }

  init_graphics_pipelines(device, window);
  init_instanced_pipeline(device, window);
  init_pbr_pipeline(device, window);
  init_compute_pipelines(device);

  // Small dummy buffer used as a valid fallback for unbound SSBOs.
  // The terrain shader declares 3 fragment storage buffers; all 3 must be
  // bound even when cluster buffers haven't been created yet.
  dummy_ssbo = gpu_create_zeroed_buffer(device, 4,
      SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);

  initialized = true;
  SDL_Log("TerrainRenderer: Initialized (graphics + compute pipelines)");
}




void TerrainRenderer::init_graphics_pipelines(SDL_GPUDevice *device, SDL_Window *window) {
  std::string shader_dir = SHADER_DIR;
  SDL_GPUTextureFormat swapchain_format =
      SDL_GetGPUSwapchainTextureFormat(device, window);


  {

    SDL_GPUShader *vert = asset_manager->load_shader(
        "terrain.vert", shader_dir + "/terrain.vert.glsl.spv",
        SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *frag = asset_manager->load_shader(
        "terrain.frag", shader_dir + "/terrain.frag.glsl.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 3);

    if (!vert || !frag) {
      if (vert) SDL_ReleaseGPUShader(device, vert);
      if (frag) SDL_ReleaseGPUShader(device, frag);
      return;
    }

    SDL_GPUVertexBufferDescription vbuf_desc = {};
    vbuf_desc.slot       = 0;
    vbuf_desc.pitch      = sizeof(BasaltVertex);
    vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;