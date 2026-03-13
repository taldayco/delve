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
  SDL_GPUBuffer *dummy_ssbo         = nullptr; // 4-byte fallback; always valid after init



  SDL_GPUTransferBuffer *counter_reset_transfer = nullptr;

  AssetManager *asset_manager = nullptr;

  uint32_t cluster_grid_w = 0;
  uint32_t cluster_grid_y = 0;


  uint32_t current_light_count = 0;


  static constexpr uint32_t MAX_LIGHTS        = 1024;
  static constexpr uint32_t MAX_LIGHT_INDICES = 65536;
};