#pragma once
#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <string>

class RadianceCascades {
public:
  void init(SDL_GPUDevice *device, const std::string &shader_dir);
  void resize(uint32_t screen_w, uint32_t screen_h);
  bool ready() const;
  SDL_GPURenderPass *begin_capture(SDL_GPUCommandBuffer *cmd);
  void end_capture(SDL_GPURenderPass *pass);
  void dispatch(SDL_GPUCommandBuffer *cmd);
  SDL_GPUTexture *fluence_texture() const;
  SDL_GPUTexture *capture_texture() const;
  SDL_GPUTexture *sdf_texture() const;
  SDL_GPUSampler *linear_sampler() const;
  SDL_GPUTextureFormat capture_depth_format() const;
  uint32_t rt_width() const;
  uint32_t rt_height() const;
  void cleanup(SDL_GPUDevice *device);

private:
  static constexpr int N_CASCADES = 4;

  void release_targets();

  SDL_GPUDevice *gpu_device = nullptr;

  SDL_GPUComputePipeline *jfa_seed_pipeline    = nullptr;
  SDL_GPUComputePipeline *jfa_step_pipeline    = nullptr;
  SDL_GPUComputePipeline *sdf_resolve_pipeline = nullptr;
  SDL_GPUComputePipeline *cascade_pipeline     = nullptr;
  SDL_GPUComputePipeline *rc_resolve_pipeline  = nullptr;

  SDL_GPUTexture *capture_tex       = nullptr;
  SDL_GPUTexture *capture_depth_tex = nullptr;
  SDL_GPUTexture *jfa_tex[2]        = {};
  SDL_GPUTexture *sdf_tex_          = nullptr;
  SDL_GPUTexture *cascade_tex[2]    = {};
  SDL_GPUTexture *fluence_tex       = nullptr;

  SDL_GPUSampler *sampler = nullptr;

  uint32_t rt_w    = 0;
  uint32_t rt_h    = 0;
  uint32_t atlas_w = 0;
  uint32_t atlas_h = 0;
};
