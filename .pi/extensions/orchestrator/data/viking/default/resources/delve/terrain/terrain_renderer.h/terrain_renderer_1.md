#pragma once
#include "terrain/terrain_mesh.h"
#include "terrain/instanced_terrain.h"
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
  SDL_GPUBuffer           *get_light_grid_ssbo()   const { return light_grid_ssbo;   }
  SDL_GPUBuffer           *get_global_index_ssbo() const { return global_index_ssbo; }

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
  void init_instanced_pipeline(SDL_GPUDevice *device, SDL_Window *window);
  void init_pbr_pipeline(SDL_GPUDevice *device, SDL_Window *window);
  void init_compute_pipelines(SDL_GPUDevice *device);
  void init_cluster_buffers(SDL_GPUDevice *device, uint32_t tilesX, uint32_t tilesY, uint32_t num_slices);



  void stage_cull_lights(SDL_GPUCommandBuffer *cmd,
                         const SceneUniforms &uniforms,
                         const std::vector<GpuPointLight> &lights);
  void stage_shaded_draw(SDL_GPURenderPass *pass, SDL_GPUCommandBuffer *cmd,
                         const SceneUniforms &uniforms);
  void stage_instanced_draw(SDL_GPURenderPass *pass, SDL_GPUCommandBuffer *cmd,
                             const SceneUniforms &uniforms);