#pragma once
#include "terrain/terrain_mesh.h"
#include "terrain/instanced_terrain.h"
#include "terrain/terrain_lighting.h"
#include "core/asset_manager.h"
#include "gpu/gpu.h"
#include <SDL3/SDL.h>
#include <vector>

class TerrainRenderer {
public:
  bool use_instanced = false;
  bool use_pbr       = false;
  InstancedTerrain *instanced_terrain = nullptr;

  void init(SDL_GPUDevice *device, SDL_Window *window, AssetManager &am);
  void upload_mesh(SDL_GPUDevice *device, const TerrainMesh &mesh);
  void upload_gltf_column_mesh(SDL_GPUDevice *device,
                                const void *vertex_data, uint32_t vertex_bytes,
                                const void *index_data, uint32_t index_bytes,
                                uint32_t index_count);
  void rebuild_dirty_pipelines(SDL_Window *window);
  void upload_light_bake(SDL_GPUDevice *device, const TerrainLightBake &bake);



  void draw(SDL_GPUCommandBuffer *cmd,
            SDL_GPUTexture *swapchain,
            uint32_t w, uint32_t h,
            const SceneUniforms &uniforms,
            const std::vector<GpuPointLight> &lights,
            UploadManager &uploader);




  void rebuild_clusters_if_needed(SDL_GPUCommandBuffer *cmd,
                                   uint32_t w, uint32_t h,
                                   float tile_px, uint32_t num_slices,
                                   float near_plane, float far_plane);


  SDL_GPURenderPass *begin_render_pass(SDL_GPUCommandBuffer *cmd,
                                       SDL_GPUTexture *swapchain,
                                       uint32_t w, uint32_t h,
                                       SDL_GPULoadOp color_load = SDL_GPU_LOADOP_CLEAR,
                                       SDL_GPULoadOp depth_load = SDL_GPU_LOADOP_CLEAR);

  SDL_GPUBuffer           *get_point_light_ssbo()  const { return point_light_ssbo;  }
  SDL_GPUBuffer           *get_light_grid_ssbo()   const { return light_grid_ssbo;   }
  SDL_GPUBuffer           *get_global_index_ssbo() const { return global_index_ssbo; }

  SDL_GPUTexture *light_texture() const { return light_bake_tex ? light_bake_tex : light_fallback_tex; }
  SDL_GPUSampler *light_sampler() const { return light_bake_smp; }

  void prepare_frame_resources(SDL_GPUDevice *device);

  bool depth_needs_rebuild() const {
    return desired_depth_w > 0 && desired_depth_h > 0 &&
           (desired_depth_w != depth_w || desired_depth_h != depth_h);
  }

  uint32_t desired_depth_w = 0;
  uint32_t desired_depth_h = 0;

  void cleanup(SDL_GPUDevice *device);

  bool is_initialized() const { return initialized; }
  bool has_mesh()       const { return has_data; }
  SDL_GPUTextureFormat get_depth_format() const { return depth_stencil_format; }

  uint32_t cluster_tiles_x() const { return cluster_grid_w; }
  uint32_t cluster_tiles_y() const { return cluster_grid_y; }

  uint32_t depth_width()  const { return depth_w; }
  uint32_t depth_height() const { return depth_h; }

private:

  void init_graphics_pipelines(SDL_GPUDevice *device, SDL_Window *window);
  void init_compute_pipelines(SDL_GPUDevice *device);
  void init_cluster_buffers(SDL_GPUDevice *device, uint32_t tilesX, uint32_t tilesY, uint32_t num_slices);

  SDL_GPUGraphicsPipeline *make_terrain_pipeline(SDL_GPUTextureFormat swapchain_format, bool pbr);
  SDL_GPUGraphicsPipeline *make_lava_pipeline(SDL_GPUTextureFormat swapchain_format);
  SDL_GPUGraphicsPipeline *make_contour_pipeline(SDL_GPUTextureFormat swapchain_format);
  SDL_GPUGraphicsPipeline *make_instanced_pipeline(SDL_GPUTextureFormat swapchain_format);



  void stage_cull_lights(SDL_GPUCommandBuffer *cmd,
                         const SceneUniforms &uniforms);
  void stage_shaded_draw(SDL_GPURenderPass *pass, SDL_GPUCommandBuffer *cmd,
                         const SceneUniforms &uniforms);
  void stage_instanced_draw(SDL_GPURenderPass *pass, SDL_GPUCommandBuffer *cmd,
                             const SceneUniforms &uniforms);


  void release_registered_buffer(SDL_GPUDevice *device, SDL_GPUBuffer *&buf, const char *key);
  void release_buffers(SDL_GPUDevice *device);
  void release_cluster_buffers(SDL_GPUDevice *device);
  void upload_lights(SDL_GPUCommandBuffer *cmd,
                     UploadManager &uploader,
                     const std::vector<GpuPointLight> &lights);


  bool initialized = false;
  bool has_data    = false;

  SDL_GPUDevice           *gpu_device           = nullptr;
  SDL_GPUTextureFormat     depth_stencil_format  = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
  SDL_GPUTexture          *depth_texture         = nullptr;
  uint32_t                 depth_w = 0, depth_h  = 0;


  SDL_GPUGraphicsPipeline *terrain_pipeline            = nullptr;
  SDL_GPUGraphicsPipeline *lava_pipeline               = nullptr;
  SDL_GPUGraphicsPipeline *contour_pipeline            = nullptr;
  SDL_GPUGraphicsPipeline *instanced_terrain_pipeline  = nullptr;
  SDL_GPUGraphicsPipeline *pbr_terrain_pipeline        = nullptr;


  SDL_GPUComputePipeline  *cluster_gen_pipeline     = nullptr;
  SDL_GPUComputePipeline  *light_culling_pipeline   = nullptr;


  SDL_GPUBuffer *basalt_vbo = nullptr;
  SDL_GPUBuffer *basalt_ibo = nullptr;
  uint32_t       basalt_side_index_count  = 0;
  uint32_t       basalt_total_index_count = 0;

  SDL_GPUBuffer *lava_vbo       = nullptr;
  SDL_GPUBuffer *lava_ibo       = nullptr;
  uint32_t       lava_vertex_count = 0;
  uint32_t       lava_index_count  = 0;

  SDL_GPUBuffer *contour_vbo    = nullptr;
  uint32_t       contour_vertex_count = 0;

  SDL_GPUBuffer *gltf_column_vbo         = nullptr;
  SDL_GPUBuffer *gltf_column_ibo         = nullptr;
  uint32_t       gltf_column_index_count = 0;


  SDL_GPUBuffer *point_light_ssbo   = nullptr;
  SDL_GPUBuffer *cluster_aabb_ssbo  = nullptr;
  SDL_GPUBuffer *light_grid_ssbo    = nullptr;
  SDL_GPUBuffer *global_index_ssbo  = nullptr;
  SDL_GPUBuffer *cull_counter_ssbo  = nullptr;
  SDL_GPUBuffer *dummy_ssbo         = nullptr;



  SDL_GPUTransferBuffer *counter_reset_transfer = nullptr;

  SDL_GPUTexture *light_bake_tex     = nullptr;
  SDL_GPUTexture *light_fallback_tex = nullptr;
  SDL_GPUSampler *light_bake_smp     = nullptr;

  AssetManager *asset_manager = nullptr;

  uint32_t cluster_grid_w = 0;
  uint32_t cluster_grid_y = 0;


  uint32_t current_light_count = 0;


  static constexpr uint32_t MAX_LIGHTS        = 1024;
  static constexpr uint32_t MAX_LIGHT_INDICES = 65536;
};
