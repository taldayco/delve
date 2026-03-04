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

    SDL_GPUVertexAttribute attrs[4] = {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, pos_x)   };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, color_r) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(BasaltVertex, sheen)   };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, nx)      };

    SDL_GPUColorTargetDescription color_desc = {};
    color_desc.format = swapchain_format;

    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader   = vert;
    pi.fragment_shader = frag;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc;
    pi.vertex_input_state.num_vertex_buffers         = 1;
    pi.vertex_input_state.vertex_attributes          = attrs;
    pi.vertex_input_state.num_vertex_attributes      = 4;
    pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions         = &color_desc;
    pi.target_info.num_color_targets                 = 1;
    pi.target_info.has_depth_stencil_target          = true;
    pi.target_info.depth_stencil_format              = depth_stencil_format;
    pi.depth_stencil_state.compare_op                = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pi.depth_stencil_state.enable_depth_test         = true;
    pi.depth_stencil_state.enable_depth_write        = true;

    terrain_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);

    asset_manager->register_pipeline("terrain", "terrain.vert", "terrain.frag");
    // Shaders are owned by the asset manager; do NOT release them here.
  }


  {
    SDL_GPUShader *vert = asset_manager->load_shader(
        "lava.vert", shader_dir + "/lava.vert.glsl.spv",
        SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *frag = asset_manager->load_shader(
        "lava.frag", shader_dir + "/lava.frag.glsl.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);

    if (vert && frag) {
      SDL_GPUVertexBufferDescription vbuf_desc = {};
      vbuf_desc.slot       = 0;
      vbuf_desc.pitch      = sizeof(GpuLavaVertex);
      vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

      SDL_GPUVertexAttribute attrs[2] = {};
      attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(GpuLavaVertex, pos_x)       };
      attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(GpuLavaVertex, time_offset) };

      SDL_GPUColorTargetDescription color_desc = {};
      color_desc.format = swapchain_format;

      SDL_GPUGraphicsPipelineCreateInfo pi = {};
      pi.vertex_shader   = vert;
      pi.fragment_shader = frag;
      pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc;
      pi.vertex_input_state.num_vertex_buffers         = 1;
      pi.vertex_input_state.vertex_attributes          = attrs;
      pi.vertex_input_state.num_vertex_attributes      = 2;
      pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
      pi.target_info.color_target_descriptions         = &color_desc;
      pi.target_info.num_color_targets                 = 1;
      pi.target_info.has_depth_stencil_target          = true;
      pi.target_info.depth_stencil_format              = depth_stencil_format;
      pi.depth_stencil_state.compare_op                = SDL_GPU_COMPAREOP_LESS;
      pi.depth_stencil_state.enable_depth_test         = true;
      pi.depth_stencil_state.enable_depth_write        = true;
      pi.depth_stencil_state.enable_stencil_test       = false;

      lava_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
      asset_manager->register_pipeline("lava", "lava.vert", "lava.frag");
    }
    // Shaders owned by asset_manager.
  }


  {
    SDL_GPUShader *vert = asset_manager->load_shader(
        "contour.vert", shader_dir + "/contour.vert.glsl.spv",
        SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *frag = asset_manager->load_shader(
        "contour.frag", shader_dir + "/contour.frag.glsl.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);

    if (vert && frag) {
      SDL_GPUVertexBufferDescription vbuf_desc = {};
      vbuf_desc.slot       = 0;
      vbuf_desc.pitch      = sizeof(ContourVertex);
      vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

      SDL_GPUVertexAttribute attrs[1] = {};
      attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 };

      SDL_GPUColorTargetDescription color_desc = {};
      color_desc.format = swapchain_format;
      color_desc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
      color_desc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      color_desc.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
      color_desc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
      color_desc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      color_desc.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
      color_desc.blend_state.enable_blend          = true;

      SDL_GPUGraphicsPipelineCreateInfo pi = {};
      pi.vertex_shader   = vert;
      pi.fragment_shader = frag;
      pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc;
      pi.vertex_input_state.num_vertex_buffers         = 1;
      pi.vertex_input_state.vertex_attributes          = attrs;
      pi.vertex_input_state.num_vertex_attributes      = 1;
      pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_LINELIST;
      pi.target_info.color_target_descriptions         = &color_desc;
      pi.target_info.num_color_targets                 = 1;
      pi.target_info.has_depth_stencil_target          = true;
      pi.target_info.depth_stencil_format              = depth_stencil_format;
      pi.depth_stencil_state.compare_op                = SDL_GPU_COMPAREOP_ALWAYS;
      pi.depth_stencil_state.enable_depth_test         = false;
      pi.depth_stencil_state.enable_depth_write        = false;

      contour_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
      asset_manager->register_pipeline("contour", "contour.vert", "contour.frag");
    }
    // Shaders owned by asset_manager.
  }

  SDL_Log("TerrainRenderer: Graphics pipelines created");
}




void TerrainRenderer::init_compute_pipelines(SDL_GPUDevice *device) {
  std::string shader_dir = SHADER_DIR;
  SDL_Log("TerrainRenderer: Loading compute shaders from %s", shader_dir.c_str());

  std::string gen_path = shader_dir + "/generate_clusters.comp.glsl.spv";
  asset_manager->load_compute_shader("generate_clusters.comp", gen_path, 1, 1, 0);
  asset_manager->register_compute_pipeline("cluster_gen", "generate_clusters.comp");

  SDL_Log("TerrainRenderer: Creating cluster_gen_pipeline from %s", gen_path.c_str());
  cluster_gen_pipeline = build_compute_pipeline(device, gen_path.c_str(), 1, 1, 0);

  std::string cull_path = shader_dir + "/light_culling.comp.glsl.spv";
  asset_manager->load_compute_shader("light_culling.comp", cull_path, 2, 5, 0);
  asset_manager->register_compute_pipeline("light_culling", "light_culling.comp");

  SDL_Log("TerrainRenderer: Creating light_culling_pipeline from %s", cull_path.c_str());
  light_culling_pipeline = build_compute_pipeline(device, cull_path.c_str(), 2, 5, 0);

  if (cluster_gen_pipeline && light_culling_pipeline)
    SDL_Log("TerrainRenderer: Compute pipelines created");
  else
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "TerrainRenderer: Failed to create one or more compute pipelines");
}




void TerrainRenderer::rebuild_dirty_pipelines(SDL_Window *window) {
  if (!asset_manager || !gpu_device) return;

  std::string shader_dir = SHADER_DIR;
  SDL_GPUTextureFormat swapchain_format =
      SDL_GetGPUSwapchainTextureFormat(gpu_device, window);

  auto rebuild_graphics = [&](const std::string &key,
                               SDL_GPUGraphicsPipeline *&pipeline_out,
                               auto pipeline_builder) {
    if (asset_manager->pipeline_needs_rebuild(key)) {
      SDL_WaitForGPUIdle(gpu_device);
      if (pipeline_out) { SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline_out); pipeline_out = nullptr; }
      pipeline_out = pipeline_builder();
      asset_manager->clear_rebuild_flag(key);
      SDL_Log("TerrainRenderer: Rebuilt pipeline '%s'", key.c_str());
    }
  };

  rebuild_graphics("terrain", terrain_pipeline, [&]() -> SDL_GPUGraphicsPipeline * {
    SDL_GPUShader *vert = asset_manager->load_shader("terrain.vert", shader_dir + "/terrain.vert.glsl.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *frag = asset_manager->load_shader("terrain.frag", shader_dir + "/terrain.frag.glsl.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 3);
    if (!vert || !frag) return nullptr;
    SDL_GPUVertexBufferDescription vbuf_desc = {};
    vbuf_desc.slot = 0; vbuf_desc.pitch = sizeof(BasaltVertex); vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[4] = {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, pos_x)   };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, color_r) };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(BasaltVertex, sheen)   };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(BasaltVertex, nx)      };
    SDL_GPUColorTargetDescription cd = {}; cd.format = swapchain_format;
    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader = vert; pi.fragment_shader = frag;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc; pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = attrs; pi.vertex_input_state.num_vertex_attributes = 4;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions = &cd; pi.target_info.num_color_targets = 1;
    pi.target_info.has_depth_stencil_target = true; pi.target_info.depth_stencil_format = depth_stencil_format;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pi.depth_stencil_state.enable_depth_test = true; pi.depth_stencil_state.enable_depth_write = true;
    return SDL_CreateGPUGraphicsPipeline(gpu_device, &pi);
  });
  rebuild_graphics("lava", lava_pipeline, [&]() -> SDL_GPUGraphicsPipeline * {
    SDL_GPUShader *vert = asset_manager->load_shader("lava.vert", shader_dir + "/lava.vert.glsl.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *frag = asset_manager->load_shader("lava.frag", shader_dir + "/lava.frag.glsl.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!vert || !frag) return nullptr;
    SDL_GPUVertexBufferDescription vbuf_desc = {};
    vbuf_desc.slot = 0; vbuf_desc.pitch = sizeof(GpuLavaVertex); vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[2] = {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(GpuLavaVertex, pos_x)       };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(GpuLavaVertex, time_offset) };
    SDL_GPUColorTargetDescription cd = {}; cd.format = swapchain_format;
    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader = vert; pi.fragment_shader = frag;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc; pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = attrs; pi.vertex_input_state.num_vertex_attributes = 2;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions = &cd; pi.target_info.num_color_targets = 1;
    pi.target_info.has_depth_stencil_target = true; pi.target_info.depth_stencil_format = depth_stencil_format;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pi.depth_stencil_state.enable_depth_test = true; pi.depth_stencil_state.enable_depth_write = true;
    return SDL_CreateGPUGraphicsPipeline(gpu_device, &pi);
  });

  rebuild_graphics("contour", contour_pipeline, [&]() -> SDL_GPUGraphicsPipeline * {
    SDL_GPUShader *vert = asset_manager->load_shader("contour.vert", shader_dir + "/contour.vert.glsl.spv", SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    SDL_GPUShader *frag = asset_manager->load_shader("contour.frag", shader_dir + "/contour.frag.glsl.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    if (!vert || !frag) return nullptr;
    SDL_GPUVertexBufferDescription vbuf_desc = {};
    vbuf_desc.slot = 0; vbuf_desc.pitch = sizeof(ContourVertex); vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[1] = {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 };
    SDL_GPUColorTargetDescription cd = {}; cd.format = swapchain_format;
    cd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    cd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    cd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    cd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    cd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    cd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    cd.blend_state.enable_blend = true;
    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader = vert; pi.fragment_shader = frag;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc; pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = attrs; pi.vertex_input_state.num_vertex_attributes = 1;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
    pi.target_info.color_target_descriptions = &cd; pi.target_info.num_color_targets = 1;
    pi.target_info.has_depth_stencil_target = true; pi.target_info.depth_stencil_format = depth_stencil_format;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    pi.depth_stencil_state.enable_depth_test = false; pi.depth_stencil_state.enable_depth_write = false;
    return SDL_CreateGPUGraphicsPipeline(gpu_device, &pi);
  });

  // Compute pipelines
  if (asset_manager->pipeline_needs_rebuild("cluster_gen")) {
    SDL_WaitForGPUIdle(gpu_device);
    if (cluster_gen_pipeline) { SDL_ReleaseGPUComputePipeline(gpu_device, cluster_gen_pipeline); cluster_gen_pipeline = nullptr; }
    std::string gen_path = shader_dir + "/generate_clusters.comp.glsl.spv";
    cluster_gen_pipeline = build_compute_pipeline(gpu_device, gen_path.c_str(), 1, 1, 0);
    asset_manager->clear_rebuild_flag("cluster_gen");
    SDL_Log("TerrainRenderer: Rebuilt pipeline 'cluster_gen'");
  }
  if (asset_manager->pipeline_needs_rebuild("light_culling")) {
    SDL_WaitForGPUIdle(gpu_device);
    if (light_culling_pipeline) { SDL_ReleaseGPUComputePipeline(gpu_device, light_culling_pipeline); light_culling_pipeline = nullptr; }
    std::string cull_path = shader_dir + "/light_culling.comp.glsl.spv";
    light_culling_pipeline = build_compute_pipeline(gpu_device, cull_path.c_str(), 2, 5, 0);
    asset_manager->clear_rebuild_flag("light_culling");
    SDL_Log("TerrainRenderer: Rebuilt pipeline 'light_culling'");
  }
}

void TerrainRenderer::init_cluster_buffers(SDL_GPUDevice *device,
                                            uint32_t tilesX, uint32_t tilesY,
                                            uint32_t num_slices) {
  release_cluster_buffers(device);

  uint32_t num_clusters = tilesX * tilesY * num_slices;


  cluster_aabb_ssbo = gpu_create_buffer(
      device,
      num_clusters * 32,
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);



  light_grid_ssbo = gpu_create_zeroed_buffer(
      device,
      num_clusters * 8,
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
      SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);


  global_index_ssbo = gpu_create_buffer(
      device,
      MAX_LIGHT_INDICES * sizeof(uint32_t),
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
      SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);


  cull_counter_ssbo = gpu_create_zeroed_buffer(
      device,
      sizeof(uint32_t),
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);


  point_light_ssbo = gpu_create_buffer(
      device,
      MAX_LIGHTS * sizeof(GpuPointLight),
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
      SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);

  cluster_grid_w = tilesX;
  cluster_grid_y = tilesY;

  if (asset_manager) {
    asset_manager->register_buffer("point_light_ssbo",  point_light_ssbo);
    asset_manager->register_buffer("cluster_aabb_ssbo", cluster_aabb_ssbo);
    asset_manager->register_buffer("light_grid_ssbo",   light_grid_ssbo);
    asset_manager->register_buffer("global_index_ssbo", global_index_ssbo);
    asset_manager->register_buffer("cull_counter_ssbo", cull_counter_ssbo);
  }

  SDL_Log("TerrainRenderer: Cluster buffers created (%u×%u×%u clusters)",
          tilesX, tilesY, num_slices);
}




void TerrainRenderer::upload_mesh(SDL_GPUDevice *device, const TerrainMesh &mesh) {
  // One GPU idle wait to ensure old buffers are no longer in use, then release them.
  SDL_WaitForGPUIdle(device);
  release_buffers(device);

  // --- Gather all CPU data first so we can size transfer buffers exactly ---

  std::vector<BasaltVertex> all_verts;
  std::vector<uint32_t>     all_indices;
  basalt_side_index_count  = 0;
  basalt_total_index_count = 0;

  if (!mesh.basalt_layers.empty() && !mesh.basalt_layers[0].vertices.empty()) {
    uint32_t vo = (uint32_t)all_verts.size();
    all_verts.insert(all_verts.end(),
                     mesh.basalt_layers[0].vertices.begin(),
                     mesh.basalt_layers[0].vertices.end());
    for (uint32_t idx : mesh.basalt_layers[0].indices)
      all_indices.push_back(idx + vo);
    basalt_side_index_count = (uint32_t)mesh.basalt_layers[0].indices.size();
  }
  if (mesh.basalt_layers.size() > 1 && !mesh.basalt_layers[1].vertices.empty()) {
    uint32_t vo = (uint32_t)all_verts.size();
    all_verts.insert(all_verts.end(),
                     mesh.basalt_layers[1].vertices.begin(),
                     mesh.basalt_layers[1].vertices.end());
    for (uint32_t idx : mesh.basalt_layers[1].indices)
      all_indices.push_back(idx + vo);
  }
  basalt_total_index_count = (uint32_t)all_indices.size();

  // --- Compute total staging size and create one shared transfer buffer ---

  uint32_t basalt_vbo_sz    = (uint32_t)(all_verts.size()                    * sizeof(BasaltVertex));
  uint32_t basalt_ibo_sz    = (uint32_t)(all_indices.size()                  * sizeof(uint32_t));
  uint32_t lava_vbo_sz      = (uint32_t)(mesh.lava_vertices.size()           * sizeof(GpuLavaVertex));
  uint32_t lava_ibo_sz      = (uint32_t)(mesh.lava_indices.size()            * sizeof(uint32_t));
  uint32_t contour_vbo_sz   = (uint32_t)(mesh.contour_vertices.size()        * sizeof(ContourVertex));

  // Align each section to 4 bytes so GPU buffer offsets are valid.
  auto align4 = [](uint32_t v) { return (v + 3u) & ~3u; };

  uint32_t off_basalt_vbo  = 0;
  uint32_t off_basalt_ibo  = off_basalt_vbo  + align4(basalt_vbo_sz);
  uint32_t off_lava_vbo    = off_basalt_ibo  + align4(basalt_ibo_sz);
  uint32_t off_lava_ibo    = off_lava_vbo    + align4(lava_vbo_sz);
  uint32_t off_contour_vbo = off_lava_ibo    + align4(lava_ibo_sz);
  uint32_t total_sz        = off_contour_vbo + align4(contour_vbo_sz);

  if (total_sz == 0) {
    has_data = false;
    return;
  }

  // Hard cap: refuse to upload meshes that would blow out GPU/CPU memory.
  constexpr uint32_t MAX_TRANSFER_SZ = 128u * 1024u * 1024u; // 128 MB
  if (total_sz > MAX_TRANSFER_SZ) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "TerrainRenderer::upload_mesh: mesh too large (%u bytes > %u limit), skipping",
                 total_sz, MAX_TRANSFER_SZ);
    has_data = false;
    return;
  }

  SDL_GPUTransferBufferCreateInfo ti = {};
  ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  ti.size  = total_sz;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &ti);
  if (!transfer) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "TerrainRenderer::upload_mesh: Failed to create transfer buffer (%u bytes): %s",
                 total_sz, SDL_GetError());
    return;
  }

  uint8_t *mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, transfer, false);
  if (!mapped) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "TerrainRenderer::upload_mesh: Failed to map transfer buffer: %s", SDL_GetError());
    return;
  }

  // Copy all sections into the staging buffer.
  if (basalt_vbo_sz)  SDL_memcpy(mapped + off_basalt_vbo,  all_verts.data(),               basalt_vbo_sz);
  if (basalt_ibo_sz)  SDL_memcpy(mapped + off_basalt_ibo,  all_indices.data(),              basalt_ibo_sz);
  if (lava_vbo_sz)    SDL_memcpy(mapped + off_lava_vbo,    mesh.lava_vertices.data(),       lava_vbo_sz);
  if (lava_ibo_sz)    SDL_memcpy(mapped + off_lava_ibo,    mesh.lava_indices.data(),        lava_ibo_sz);
  if (contour_vbo_sz) SDL_memcpy(mapped + off_contour_vbo, mesh.contour_vertices.data(),    contour_vbo_sz);

  SDL_UnmapGPUTransferBuffer(device, transfer);

  // --- Create all GPU buffers ---

  if (basalt_vbo_sz && basalt_ibo_sz) {
    basalt_vbo = gpu_create_buffer(device, basalt_vbo_sz,  SDL_GPU_BUFFERUSAGE_VERTEX);
    basalt_ibo = gpu_create_buffer(device, basalt_ibo_sz,  SDL_GPU_BUFFERUSAGE_INDEX);
  }
  if (lava_vbo_sz) {
    lava_vbo          = gpu_create_buffer(device, lava_vbo_sz,    SDL_GPU_BUFFERUSAGE_VERTEX);
    lava_vertex_count = (uint32_t)mesh.lava_vertices.size();
  }
  if (lava_ibo_sz) {
    lava_ibo          = gpu_create_buffer(device, lava_ibo_sz,    SDL_GPU_BUFFERUSAGE_INDEX);
    lava_index_count  = (uint32_t)mesh.lava_indices.size();
  }
  if (contour_vbo_sz) {
    contour_vbo          = gpu_create_buffer(device, contour_vbo_sz, SDL_GPU_BUFFERUSAGE_VERTEX);
    contour_vertex_count = (uint32_t)mesh.contour_vertices.size();
  }

  // --- One command buffer, one copy pass, all uploads ---

  SDL_GPUCommandBuffer *cmd  = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass      *copy = SDL_BeginGPUCopyPass(cmd);

  auto upload = [&](SDL_GPUBuffer *buf, uint32_t offset, uint32_t size) {
    if (!buf || size == 0) return;
    SDL_GPUTransferBufferLocation src = { transfer, offset };
    SDL_GPUBufferRegion           dst = { buf, 0, size };
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
  };

  upload(basalt_vbo,  off_basalt_vbo,  basalt_vbo_sz);
  upload(basalt_ibo,  off_basalt_ibo,  basalt_ibo_sz);
  upload(lava_vbo,    off_lava_vbo,    lava_vbo_sz);
  upload(lava_ibo,    off_lava_ibo,    lava_ibo_sz);
  upload(contour_vbo, off_contour_vbo, contour_vbo_sz);

  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);

  // One final wait so the transfer buffer is safe to release.
  SDL_WaitForGPUIdle(device);
  SDL_ReleaseGPUTransferBuffer(device, transfer);

  // --- Register buffers with asset manager ---

  if (asset_manager) {
    if (basalt_vbo)  asset_manager->register_buffer("basalt_vbo",  basalt_vbo);
    if (basalt_ibo)  asset_manager->register_buffer("basalt_ibo",  basalt_ibo);
    if (lava_vbo)    asset_manager->register_buffer("lava_vbo",    lava_vbo);
    if (lava_ibo)    asset_manager->register_buffer("lava_ibo",    lava_ibo);
    if (contour_vbo) asset_manager->register_buffer("contour_vbo", contour_vbo);
  }

  has_data = true;
  SDL_Log("TerrainRenderer: Mesh uploaded (basalt=%u idx, lava=%u verts/%u idx, contour=%u verts) staging=%u bytes",
          basalt_total_index_count, lava_vertex_count, lava_index_count,
          contour_vertex_count, total_sz);
}




void TerrainRenderer::upload_lights(SDL_GPUCommandBuffer *cmd,
                                     UploadManager &uploader,
                                     const std::vector<GpuPointLight> &lights) {
  if (!point_light_ssbo || lights.empty()) {
    current_light_count = 0;
    return;
  }

  uint32_t count     = (uint32_t)std::min(lights.size(), (size_t)MAX_LIGHTS);
  uint32_t byte_size = count * (uint32_t)sizeof(GpuPointLight);

  uint32_t offset = 0;
  void *dst_ptr   = uploader.alloc(byte_size, &offset);

  if (!dst_ptr) {
    // UploadManager overflow — fall back to a one-shot transfer buffer.
    SDL_GPUTransferBufferCreateInfo ti = {};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size  = byte_size;
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gpu_device, &ti);
    if (!transfer) { current_light_count = 0; return; }
    void *mapped = SDL_MapGPUTransferBuffer(gpu_device, transfer, false);
    if (!mapped) { SDL_ReleaseGPUTransferBuffer(gpu_device, transfer); current_light_count = 0; return; }
    SDL_memcpy(mapped, lights.data(), byte_size);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { transfer, 0 };
    SDL_GPUBufferRegion           dst_reg = { point_light_ssbo, 0, byte_size };
    SDL_UploadToGPUBuffer(copy, &src, &dst_reg, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer);
  } else {
    SDL_memcpy(dst_ptr, lights.data(), byte_size);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { uploader.buffer, offset };
    SDL_GPUBufferRegion           dst_reg = { point_light_ssbo, 0, byte_size };
    SDL_UploadToGPUBuffer(copy, &src, &dst_reg, false);
    SDL_EndGPUCopyPass(copy);
  }

  current_light_count = count;
  static bool logged_count = false;
  if (!logged_count && count > 0) {
    SDL_Log("TerrainRenderer: Uploaded %u lights", count);
    logged_count = true;
  }
}




void TerrainRenderer::rebuild_clusters_if_needed(SDL_GPUCommandBuffer *cmd,
                                                  uint32_t w, uint32_t h,
                                                  float tile_px, uint32_t num_slices,
                                                  float near_plane, float far_plane) {
  uint32_t tilesX = (uint32_t)std::ceil(w / tile_px);
  uint32_t tilesY = (uint32_t)std::ceil(h / tile_px);

  if (tilesX == cluster_grid_w && tilesY == cluster_grid_y) return;


  init_cluster_buffers(gpu_device, tilesX, tilesY, num_slices);

  if (!cluster_gen_pipeline || !cluster_aabb_ssbo) return;


  struct ClusterGenUniforms {
    float tile_px, grid_size_x, grid_size_y, num_slices;
    float near_plane, far_plane, screen_w, screen_h;
    float _pad0, _pad1;
  } cu;
  cu.tile_px     = tile_px;
  cu.grid_size_x = (float)tilesX;
  cu.grid_size_y = (float)tilesY;
  cu.num_slices  = (float)num_slices;
  cu.near_plane  = near_plane;
  cu.far_plane   = far_plane;
  cu.screen_w    = (float)w;
  cu.screen_h    = (float)h;
  cu._pad0 = cu._pad1 = 0.0f;

  SDL_GPUStorageBufferReadWriteBinding rw_binds[1] = {};
  rw_binds[0].buffer = cluster_aabb_ssbo;


  SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, nullptr, 0, rw_binds, 1);
  SDL_BindGPUComputePipeline(pass, cluster_gen_pipeline);
  SDL_PushGPUComputeUniformData(cmd, 0, &cu, sizeof(cu));



  uint32_t dispX = (tilesX + 15) / 16;
  uint32_t dispY = (tilesY + 8) / 9;
  SDL_DispatchGPUCompute(pass, dispX, dispY, num_slices);
  SDL_EndGPUComputePass(pass);
}







void TerrainRenderer::stage_cull_lights(SDL_GPUCommandBuffer *cmd,
                                         const SceneUniforms &u,
                                         const std::vector<GpuPointLight> &lights) {
  if (!light_culling_pipeline || !cluster_aabb_ssbo || !light_grid_ssbo ||
      !global_index_ssbo || !point_light_ssbo || !cull_counter_ssbo)
    return;







  {

    if (!counter_reset_transfer) {
      SDL_GPUTransferBufferCreateInfo ti = {};
      ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
      ti.size  = sizeof(uint32_t);
      counter_reset_transfer = SDL_CreateGPUTransferBuffer(gpu_device, &ti);
    }

    if (counter_reset_transfer) {
      uint32_t *mapped = (uint32_t *)SDL_MapGPUTransferBuffer(gpu_device, counter_reset_transfer, true);
      if (mapped) {
        *mapped = 0;
        SDL_UnmapGPUTransferBuffer(gpu_device, counter_reset_transfer);

        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src = { counter_reset_transfer, 0 };
        SDL_GPUBufferRegion           dst = { cull_counter_ssbo, 0, sizeof(uint32_t) };
        SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        SDL_EndGPUCopyPass(copy);
      }
    }
  }


  struct CullUniforms {
    float tile_px, grid_size_x, grid_size_y, num_slices;
    float near_plane, far_plane, screen_w, screen_h;
    float light_count_f, _pad0, _pad1, _pad2;
  } cu;
  static_assert(sizeof(CullUniforms) == 48, "CullUniforms must be 48 bytes");
  cu.tile_px       = u.tile_px;
  cu.grid_size_x   = u.grid_size_x;
  cu.grid_size_y   = u.grid_size_y;
  cu.num_slices    = u.num_slices;
  cu.near_plane    = u.near_plane;
  cu.far_plane     = u.far_plane;
  cu.screen_w      = u.grid_size_x * u.tile_px;
  cu.screen_h      = u.grid_size_y * u.tile_px;
  cu.light_count_f = (float)current_light_count;
  cu._pad0 = cu._pad1 = cu._pad2 = 0.0f;
  glm::mat4 view_proj = u.projection * u.view;



  SDL_GPUStorageBufferReadWriteBinding rw[5] = {};
  rw[0].buffer = point_light_ssbo;
  rw[1].buffer = cluster_aabb_ssbo;
  rw[2].buffer = light_grid_ssbo;
  rw[3].buffer = global_index_ssbo;
  rw[4].buffer = cull_counter_ssbo;

  SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, nullptr, 0, rw, 5);
  SDL_BindGPUComputePipeline(pass, light_culling_pipeline);
  SDL_PushGPUComputeUniformData(cmd, 0, &cu, sizeof(cu));
  SDL_PushGPUComputeUniformData(cmd, 1, &view_proj, sizeof(view_proj));


  uint32_t dispX = (cluster_grid_w + 15) / 16;
  uint32_t dispY = (cluster_grid_y + 8) / 9;
  uint32_t dispZ = 24;
  SDL_DispatchGPUCompute(pass, dispX, dispY, dispZ);
  SDL_EndGPUComputePass(pass);
}




void TerrainRenderer::stage_shaded_draw(SDL_GPURenderPass *pass,
                                         SDL_GPUCommandBuffer *cmd,
                                         const SceneUniforms &uniforms) {

  if (basalt_vbo && basalt_ibo && basalt_total_index_count > 0 && terrain_pipeline) {
    SDL_BindGPUGraphicsPipeline(pass, terrain_pipeline);
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

    {
      SDL_GPUBuffer *frag_storage[3] = {
        point_light_ssbo  ? point_light_ssbo  : dummy_ssbo,
        light_grid_ssbo   ? light_grid_ssbo   : dummy_ssbo,
        global_index_ssbo ? global_index_ssbo : dummy_ssbo,
      };
      SDL_BindGPUFragmentStorageBuffers(pass, 0, frag_storage, 3);
    }

    SDL_GPUBufferBinding vbind = { basalt_vbo, 0 };
    SDL_GPUBufferBinding ibind = { basalt_ibo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
    SDL_BindGPUIndexBuffer(pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(pass, basalt_total_index_count, 1, 0, 0, 0);
  }


  if (lava_vbo && lava_ibo && lava_index_count > 0 && lava_pipeline) {
    SDL_BindGPUGraphicsPipeline(pass, lava_pipeline);
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
    SDL_GPUBufferBinding vbind = { lava_vbo, 0 };
    SDL_GPUBufferBinding ibind = { lava_ibo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
    SDL_BindGPUIndexBuffer(pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(pass, lava_index_count, 1, 0, 0, 0);
  }


  if (post_terrain_draw_fn)
    post_terrain_draw_fn(pass, cmd, uniforms);

  if (contour_vbo && contour_vertex_count > 0 && contour_pipeline) {
    SDL_BindGPUGraphicsPipeline(pass, contour_pipeline);
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
    SDL_GPUBufferBinding vbind = { contour_vbo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
    SDL_DrawGPUPrimitives(pass, contour_vertex_count, 1, 0, 0);
  }
}




void TerrainRenderer::draw(SDL_GPUCommandBuffer *cmd,
                            SDL_GPUTexture *swapchain,
                            uint32_t w, uint32_t h,
                            const SceneUniforms &uniforms,
                            const std::vector<GpuPointLight> &lights,
                            UploadManager &uploader) {
  if (!initialized || !has_data) return;

  upload_lights(cmd, uploader, lights);
  stage_cull_lights(cmd, uniforms, lights);

  SDL_GPURenderPass *pass = begin_render_pass_load(cmd, swapchain, w, h);
  if (!pass) return;
  stage_shaded_draw(pass, cmd, uniforms);
  SDL_EndGPURenderPass(pass);
}




SDL_GPURenderPass *TerrainRenderer::begin_render_pass(SDL_GPUCommandBuffer *cmd,
                                                       SDL_GPUTexture *swapchain,
                                                       uint32_t w, uint32_t h) {

  desired_depth_w = w;
  desired_depth_h = h;
  if (!depth_texture || depth_w != w || depth_h != h) return nullptr;

  SDL_GPUColorTargetInfo color_target = {};
  color_target.texture     = swapchain;
  color_target.load_op     = SDL_GPU_LOADOP_CLEAR;
  color_target.store_op    = SDL_GPU_STOREOP_STORE;
  color_target.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
  color_target.cycle       = false;

  SDL_GPUDepthStencilTargetInfo depth_target = {};
  depth_target.texture          = depth_texture;
  depth_target.load_op          = SDL_GPU_LOADOP_CLEAR;
  depth_target.store_op         = SDL_GPU_STOREOP_STORE;
  depth_target.clear_depth      = 1.0f;
  depth_target.stencil_load_op  = SDL_GPU_LOADOP_CLEAR;
  depth_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
  depth_target.clear_stencil    = 0;
  depth_target.cycle            = false;

  return SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
}




SDL_GPURenderPass *TerrainRenderer::begin_render_pass_load(SDL_GPUCommandBuffer *cmd,
                                                            SDL_GPUTexture *swapchain,
                                                            uint32_t w, uint32_t h) {

  desired_depth_w = w;
  desired_depth_h = h;
  if (!depth_texture || depth_w != w || depth_h != h) return nullptr;

  SDL_GPUColorTargetInfo color_target = {};
  color_target.texture   = swapchain;
  color_target.load_op   = SDL_GPU_LOADOP_LOAD;
  color_target.store_op  = SDL_GPU_STOREOP_STORE;
  color_target.cycle     = false;


  SDL_GPUDepthStencilTargetInfo depth_target = {};
  depth_target.texture          = depth_texture;
  depth_target.load_op          = SDL_GPU_LOADOP_CLEAR;
  depth_target.store_op         = SDL_GPU_STOREOP_STORE;
  depth_target.clear_depth      = 1.0f;
  depth_target.stencil_load_op  = SDL_GPU_LOADOP_CLEAR;
  depth_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
  depth_target.clear_stencil    = 0;
  depth_target.cycle            = false;

  return SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
}



SDL_GPURenderPass *TerrainRenderer::begin_render_pass_load_preserve_depth(
    SDL_GPUCommandBuffer *cmd,
    SDL_GPUTexture *swapchain,
    uint32_t w, uint32_t h) {

  desired_depth_w = w;
  desired_depth_h = h;
  if (!depth_texture || depth_w != w || depth_h != h) return nullptr;

  SDL_GPUColorTargetInfo color_target = {};
  color_target.texture  = swapchain;
  color_target.load_op  = SDL_GPU_LOADOP_LOAD;
  color_target.store_op = SDL_GPU_STOREOP_STORE;
  color_target.cycle    = false;

  SDL_GPUDepthStencilTargetInfo depth_target = {};
  depth_target.texture          = depth_texture;
  depth_target.load_op          = SDL_GPU_LOADOP_LOAD;
  depth_target.store_op         = SDL_GPU_STOREOP_STORE;
  depth_target.stencil_load_op  = SDL_GPU_LOADOP_LOAD;
  depth_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
  depth_target.cycle            = false;

  return SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
}



void TerrainRenderer::prepare_frame_resources(SDL_GPUDevice *device) {
  if (desired_depth_w == 0 || desired_depth_h == 0) return;
  if (depth_texture && depth_w == desired_depth_w && depth_h == desired_depth_h) return;

  // Caller (on_pre_frame_game) has already called SDL_WaitForGPUIdle.
  if (depth_texture) {
    SDL_ReleaseGPUTexture(device, depth_texture);
    depth_texture = nullptr;
  }

  SDL_GPUTextureCreateInfo ti = {};
  ti.type                 = SDL_GPU_TEXTURETYPE_2D;
  ti.format               = depth_stencil_format;
  ti.width                = desired_depth_w;
  ti.height               = desired_depth_h;
  ti.layer_count_or_depth = 1;
  ti.num_levels           = 1;
  ti.usage                = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  depth_texture           = SDL_CreateGPUTexture(device, &ti);
  depth_w = desired_depth_w;
  depth_h = desired_depth_h;

  SDL_Log("TerrainRenderer: Depth texture (re)created (%ux%u)", depth_w, depth_h);
}

void TerrainRenderer::release_buffers(SDL_GPUDevice *device) {
  auto rel = [&](SDL_GPUBuffer *&buf, const char *key) {
    if (!buf) return;
    if (asset_manager) { asset_manager->release_buffer(key); }
    else               { SDL_ReleaseGPUBuffer(device, buf); }
    buf = nullptr;
  };
  rel(basalt_vbo,  "basalt_vbo");
  rel(basalt_ibo,  "basalt_ibo");
  rel(lava_vbo,    "lava_vbo");
  rel(lava_ibo,    "lava_ibo");
  rel(contour_vbo, "contour_vbo");
  has_data = false;
}

void TerrainRenderer::release_cluster_buffers(SDL_GPUDevice *device) {
  // Release through asset manager when available so the registry stays consistent.
  auto rel = [&](SDL_GPUBuffer *&buf, const char *key) {
    if (!buf) return;
    if (asset_manager) { asset_manager->release_buffer(key); }
    else               { SDL_ReleaseGPUBuffer(device, buf); }
    buf = nullptr;
  };
  rel(point_light_ssbo,  "point_light_ssbo");
  rel(cluster_aabb_ssbo, "cluster_aabb_ssbo");
  rel(light_grid_ssbo,   "light_grid_ssbo");
  rel(global_index_ssbo, "global_index_ssbo");
  rel(cull_counter_ssbo, "cull_counter_ssbo");
  if (counter_reset_transfer) { SDL_ReleaseGPUTransferBuffer(device, counter_reset_transfer); counter_reset_transfer = nullptr; }
  cluster_grid_w = 0;
  cluster_grid_y = 0;
}

void TerrainRenderer::cleanup(SDL_GPUDevice *device) {
  SDL_WaitForGPUIdle(device);

  release_buffers(device);
  release_cluster_buffers(device);

  if (dummy_ssbo)               { SDL_ReleaseGPUBuffer(device, dummy_ssbo);                           dummy_ssbo               = nullptr; }
  if (depth_texture)            { SDL_ReleaseGPUTexture(device, depth_texture);                        depth_texture            = nullptr; }
  if (terrain_pipeline)         { SDL_ReleaseGPUGraphicsPipeline(device, terrain_pipeline);            terrain_pipeline         = nullptr; }
  if (lava_pipeline)            { SDL_ReleaseGPUGraphicsPipeline(device, lava_pipeline);               lava_pipeline            = nullptr; }
  if (contour_pipeline)         { SDL_ReleaseGPUGraphicsPipeline(device, contour_pipeline);            contour_pipeline         = nullptr; }
  if (cluster_gen_pipeline)     { SDL_ReleaseGPUComputePipeline(device, cluster_gen_pipeline);         cluster_gen_pipeline     = nullptr; }
  if (light_culling_pipeline)   { SDL_ReleaseGPUComputePipeline(device, light_culling_pipeline);       light_culling_pipeline   = nullptr; }
  // Shaders are owned by AssetManager and released via asset_manager.clear().

  initialized = false;
  SDL_Log("TerrainRenderer: Cleaned up");
}
