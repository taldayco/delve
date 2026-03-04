#pragma once
#include "terrain/terrain_mesh.h"
#include "core/asset_manager.h"
#include "gpu/gpu.h"
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

class TerrainRenderer {
public:
  void init(SDL_GPUDevice *device, SDL_Window *window, AssetManager &am);
  void upload_mesh(SDL_GPUDevice *device, const TerrainMesh &mesh);
  void rebuild_dirty_pipelines(SDL_Window *window);



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
                                       uint32_t w, uint32_t h);

  SDL_GPURenderPass *begin_render_pass_load(SDL_GPUCommandBuffer *cmd,
                                            SDL_GPUTexture *swapchain,
                                            uint32_t w, uint32_t h);

  SDL_GPURenderPass *begin_render_pass_load_preserve_depth(SDL_GPUCommandBuffer *cmd,
                                                           SDL_GPUTexture *swapchain,
                                                           uint32_t w, uint32_t h);

  SDL_GPUBuffer           *get_point_light_ssbo()  const { return point_light_ssbo;  }
  SDL_GPUGraphicsPipeline *get_terrain_pipeline()  const { return terrain_pipeline;  }
  SDL_GPUBuffer           *get_dummy_ssbo()        const { return dummy_ssbo;        }

  // Called from on_pre_frame_game (no frame cmd buf open). Releases and recreates
  // the depth texture if desired_depth_w/h differ from current depth_w/h.
  // Caller must have already called SDL_WaitForGPUIdle before invoking this.
  void prepare_frame_resources(SDL_GPUDevice *device);

  // Returns true if the depth texture needs to be (re)created before the next frame.
  bool depth_needs_rebuild() const {
    return desired_depth_w > 0 && desired_depth_h > 0 &&
           (desired_depth_w != depth_w || desired_depth_h != depth_h);
  }

  // Requested depth texture dimensions (set by begin_render_pass from swapchain size).
  uint32_t desired_depth_w = 0;
  uint32_t desired_depth_h = 0;

  void cleanup(SDL_GPUDevice *device);

  // Optional callback invoked after lava draw and before contour lines.
  // Signature: void(SDL_GPURenderPass*, SDL_GPUCommandBuffer*, const SceneUniforms&)
  std::function<void(SDL_GPURenderPass *, SDL_GPUCommandBuffer *, const SceneUniforms &)> post_terrain_draw_fn;

  bool is_initialized() const { return initialized; }
  bool has_mesh()       const { return has_data; }
  SDL_GPUTextureFormat get_depth_format() const { return depth_stencil_format; }

  uint32_t cluster_tiles_x() const { return cluster_grid_w; }
  uint32_t cluster_tiles_y() const { return cluster_grid_y; }

  // Current depth texture dimensions (0 until first prepare_frame_resources call).
  uint32_t depth_width()  const { return depth_w; }
  uint32_t depth_height() const { return depth_h; }

private:

  void init_graphics_pipelines(SDL_GPUDevice *device, SDL_Window *window);
  void init_compute_pipelines(SDL_GPUDevice *device);
  void init_cluster_buffers(SDL_GPUDevice *device, uint32_t tilesX, uint32_t tilesY, uint32_t num_slices);



  void stage_cull_lights(SDL_GPUCommandBuffer *cmd,
                         const SceneUniforms &uniforms,
                         const std::vector<GpuPointLight> &lights);
  void stage_shaded_draw(SDL_GPURenderPass *pass, SDL_GPUCommandBuffer *cmd,
                         const SceneUniforms &uniforms);


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


  SDL_GPUGraphicsPipeline *terrain_pipeline         = nullptr;
  SDL_GPUGraphicsPipeline *lava_pipeline            = nullptr;
  SDL_GPUGraphicsPipeline *contour_pipeline         = nullptr;


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


  SDL_GPUBuffer *point_light_ssbo   = nullptr;
  SDL_GPUBuffer *cluster_aabb_ssbo  = nullptr;
  SDL_GPUBuffer *light_grid_ssbo    = nullptr;
  SDL_GPUBuffer *global_index_ssbo  = nullptr;
  SDL_GPUBuffer *cull_counter_ssbo  = nullptr;
  SDL_GPUBuffer *dummy_ssbo         = nullptr; // 4-byte fallback; always valid after init



  SDL_GPUTransferBuffer *counter_reset_transfer = nullptr;

  AssetManager *asset_manager = nullptr;

  uint32_t cluster_grid_w = 0;
  uint32_t cluster_grid_y = 0;


  uint32_t current_light_count = 0;


  static constexpr uint32_t MAX_LIGHTS        = 1024;
  static constexpr uint32_t MAX_LIGHT_INDICES = 65536;
};
