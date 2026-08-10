#include "render/radiance_cascades.h"
#include "gpu/gpu.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <vector>

static SDL_GPUComputePipeline *load_rc_compute(SDL_GPUDevice *device,
                                               const std::string &path,
                                               int num_samplers,
                                               int num_uniform_buffers) {
  SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb");
  if (!io) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "load_rc_compute: Failed to open %s", path.c_str());
    return nullptr;
  }
  Sint64 size = SDL_GetIOSize(io);
  if (size <= 0) { SDL_CloseIO(io); return nullptr; }
  std::vector<uint8_t> code(size);
  SDL_ReadIO(io, code.data(), size);
  SDL_CloseIO(io);

  SDL_GPUComputePipelineCreateInfo info = {};
  info.code                           = code.data();
  info.code_size                      = (size_t)size;
  info.entrypoint                     = "main";
  info.format                         = SDL_GPU_SHADERFORMAT_SPIRV;
  info.num_samplers                   = (Uint32)num_samplers;
  info.num_readwrite_storage_textures = 1;
  info.num_uniform_buffers            = (Uint32)num_uniform_buffers;
  info.threadcount_x                  = 16;
  info.threadcount_y                  = 16;
  info.threadcount_z                  = 1;

  SDL_GPUComputePipeline *pipeline = SDL_CreateGPUComputePipeline(device, &info);
  if (!pipeline)
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "load_rc_compute: Failed to create from %s: %s",
                 path.c_str(), SDL_GetError());
  return pipeline;
}

static SDL_GPUTexture *create_rc_texture(SDL_GPUDevice *device,
                                         SDL_GPUTextureFormat format,
                                         uint32_t w, uint32_t h,
                                         SDL_GPUTextureUsageFlags usage) {
  SDL_GPUTextureCreateInfo ti = {};
  ti.type                 = SDL_GPU_TEXTURETYPE_2D;
  ti.format               = format;
  ti.width                = w;
  ti.height               = h;
  ti.layer_count_or_depth = 1;
  ti.num_levels           = 1;
  ti.usage                = usage;
  SDL_GPUTexture *tex = SDL_CreateGPUTexture(device, &ti);
  if (!tex)
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "RadianceCascades: Failed to create texture (%ux%u): %s",
                 w, h, SDL_GetError());
  return tex;
}

static uint32_t groups(uint32_t extent, uint32_t local) {
  return (extent + local - 1) / local;
}

void RadianceCascades::init(SDL_GPUDevice *device, const std::string &shader_dir) {
  if (gpu_device) return;
  gpu_device = device;

  jfa_seed_pipeline    = load_rc_compute(device, shader_dir + "/jfa_seed.comp.glsl.spv",    1, 0);
  jfa_step_pipeline    = load_rc_compute(device, shader_dir + "/jfa_step.comp.glsl.spv",    1, 1);
  sdf_resolve_pipeline = load_rc_compute(device, shader_dir + "/sdf_resolve.comp.glsl.spv", 1, 0);
  cascade_pipeline     = load_rc_compute(device, shader_dir + "/rc_cascade.comp.glsl.spv",  3, 1);
  rc_resolve_pipeline  = load_rc_compute(device, shader_dir + "/rc_resolve.comp.glsl.spv",  1, 1);

  sampler = gpu_create_linear_clamp_sampler(device);

  SDL_Log("RadianceCascades: Initialized (pipelines %s)",
          (jfa_seed_pipeline && jfa_step_pipeline && sdf_resolve_pipeline &&
           cascade_pipeline && rc_resolve_pipeline) ? "ok" : "FAILED");
}

void RadianceCascades::release_targets() {
  if (!gpu_device) return;
  if (capture_tex)       { SDL_ReleaseGPUTexture(gpu_device, capture_tex);       capture_tex = nullptr; }
  if (capture_depth_tex) { SDL_ReleaseGPUTexture(gpu_device, capture_depth_tex); capture_depth_tex = nullptr; }
  for (int i = 0; i < 2; ++i) {
    if (jfa_tex[i])     { SDL_ReleaseGPUTexture(gpu_device, jfa_tex[i]);     jfa_tex[i] = nullptr; }
    if (cascade_tex[i]) { SDL_ReleaseGPUTexture(gpu_device, cascade_tex[i]); cascade_tex[i] = nullptr; }
  }
  if (sdf_tex_)    { SDL_ReleaseGPUTexture(gpu_device, sdf_tex_);    sdf_tex_ = nullptr; }
  if (fluence_tex) { SDL_ReleaseGPUTexture(gpu_device, fluence_tex); fluence_tex = nullptr; }
}

void RadianceCascades::resize(uint32_t screen_w, uint32_t screen_h) {
  if (!gpu_device) return;
  uint32_t new_w = (screen_w + 1) / 2;
  uint32_t new_h = (screen_h + 1) / 2;
  if (new_w == rt_w && new_h == rt_h && capture_tex) return;

  release_targets();
  rt_w    = new_w;
  rt_h    = new_h;
  atlas_w = rt_w + 8;
  atlas_h = rt_h + 8;
  if (rt_w == 0 || rt_h == 0) return;

  const SDL_GPUTextureUsageFlags storage_usage =
      SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

  capture_tex       = create_rc_texture(gpu_device, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                                        rt_w, rt_h,
                                        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                        SDL_GPU_TEXTUREUSAGE_SAMPLER);
  capture_depth_tex = create_rc_texture(gpu_device, capture_depth_format(), rt_w, rt_h,
                                        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
  for (int i = 0; i < 2; ++i) {
    jfa_tex[i]     = create_rc_texture(gpu_device, SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT,
                                       rt_w, rt_h, storage_usage);
    cascade_tex[i] = create_rc_texture(gpu_device, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                                       atlas_w, atlas_h, storage_usage);
  }
  sdf_tex_    = create_rc_texture(gpu_device, SDL_GPU_TEXTUREFORMAT_R16_FLOAT,
                                  rt_w, rt_h, storage_usage);
  fluence_tex = create_rc_texture(gpu_device, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                                  rt_w, rt_h, storage_usage);

  SDL_Log("RadianceCascades: Targets resized to %ux%u (atlas %ux%u)",
          rt_w, rt_h, atlas_w, atlas_h);
}

bool RadianceCascades::ready() const {
  return jfa_seed_pipeline && jfa_step_pipeline && sdf_resolve_pipeline &&
         cascade_pipeline && rc_resolve_pipeline && sampler &&
         capture_tex && capture_depth_tex && jfa_tex[0] && jfa_tex[1] &&
         sdf_tex_ && cascade_tex[0] && cascade_tex[1] && fluence_tex;
}

SDL_GPURenderPass *RadianceCascades::begin_capture(SDL_GPUCommandBuffer *cmd) {
  if (!ready()) return nullptr;

  SDL_GPUColorTargetInfo color = {};
  color.texture     = capture_tex;
  color.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
  color.load_op     = SDL_GPU_LOADOP_CLEAR;
  color.store_op    = SDL_GPU_STOREOP_STORE;

  SDL_GPUDepthStencilTargetInfo depth = {};
  depth.texture          = capture_depth_tex;
  depth.clear_depth      = 1.0f;
  depth.load_op          = SDL_GPU_LOADOP_CLEAR;
  depth.store_op         = SDL_GPU_STOREOP_DONT_CARE;
  depth.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
  depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

  return SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
}

void RadianceCascades::end_capture(SDL_GPURenderPass *pass) {
  if (pass) SDL_EndGPURenderPass(pass);
}

void RadianceCascades::dispatch(SDL_GPUCommandBuffer *cmd) {
  if (!ready()) return;

  const uint32_t disp_x = groups(rt_w, 16);
  const uint32_t disp_y = groups(rt_h, 16);

  {
    SDL_GPUStorageTextureReadWriteBinding rw = {};
    rw.texture = jfa_tex[0];
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
    SDL_BindGPUComputePipeline(pass, jfa_seed_pipeline);
    SDL_GPUTextureSamplerBinding smp = { capture_tex, sampler };
    SDL_BindGPUComputeSamplers(pass, 0, &smp, 1);
    SDL_DispatchGPUCompute(pass, disp_x, disp_y, 1);
    SDL_EndGPUComputePass(pass);
  }

  struct JfaStepParams { int32_t jump, rt_w, rt_h, _pad0; } jp;
  static_assert(sizeof(JfaStepParams) == 16, "JfaStepParams must be 16 bytes");
  uint32_t max_dim = std::max(rt_w, rt_h);
  uint32_t n_steps = 0;
  while ((1u << n_steps) < max_dim) ++n_steps;

  int jfa_read = 0, jfa_write = 1;
  for (uint32_t s = 0; s < n_steps; ++s) {
    jp.jump  = (int32_t)(1u << (n_steps - 1 - s));
    jp.rt_w  = (int32_t)rt_w;
    jp.rt_h  = (int32_t)rt_h;
    jp._pad0 = 0;

    SDL_GPUStorageTextureReadWriteBinding rw = {};
    rw.texture = jfa_tex[jfa_write];
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
    SDL_BindGPUComputePipeline(pass, jfa_step_pipeline);
    SDL_GPUTextureSamplerBinding smp = { jfa_tex[jfa_read], sampler };
    SDL_BindGPUComputeSamplers(pass, 0, &smp, 1);
    SDL_PushGPUComputeUniformData(cmd, 0, &jp, sizeof(jp));
    SDL_DispatchGPUCompute(pass, disp_x, disp_y, 1);
    SDL_EndGPUComputePass(pass);
    std::swap(jfa_read, jfa_write);
  }

  {
    SDL_GPUStorageTextureReadWriteBinding rw = {};
    rw.texture = sdf_tex_;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
    SDL_BindGPUComputePipeline(pass, sdf_resolve_pipeline);
    SDL_GPUTextureSamplerBinding smp = { jfa_tex[jfa_read], sampler };
    SDL_BindGPUComputeSamplers(pass, 0, &smp, 1);
    SDL_DispatchGPUCompute(pass, disp_x, disp_y, 1);
    SDL_EndGPUComputePass(pass);
  }

  struct CascadeParams {
    int32_t cascade_index, dirs, dirs_sqrt, spacing;
    float   t_start, t_len;
    int32_t atlas_w, atlas_h;
    int32_t rt_w, rt_h, is_top, _pad0;
  } cp;
  static_assert(sizeof(CascadeParams) == 48, "CascadeParams must be 48 bytes");

  int c_read = 0, c_write = 1;
  for (int i = N_CASCADES - 1; i >= 0; --i) {
    uint32_t spacing   = 2u << i;
    uint32_t dirs_sqrt = 2u << i;
    uint32_t four_pow  = 1u << (2 * i);
    cp.cascade_index = i;
    cp.dirs          = (int32_t)(4u * four_pow);
    cp.dirs_sqrt     = (int32_t)dirs_sqrt;
    cp.spacing       = (int32_t)spacing;
    cp.t_start       = 2.0f * (float)(four_pow - 1u);
    cp.t_len         = 6.0f * (float)four_pow;
    cp.atlas_w       = (int32_t)atlas_w;
    cp.atlas_h       = (int32_t)atlas_h;
    cp.rt_w          = (int32_t)rt_w;
    cp.rt_h          = (int32_t)rt_h;
    cp.is_top        = (i == N_CASCADES - 1) ? 1 : 0;
    cp._pad0         = 0;

    uint32_t probes_x = groups(rt_w, spacing);
    uint32_t probes_y = groups(rt_h, spacing);
    uint32_t used_w   = std::min(probes_x * dirs_sqrt, atlas_w);
    uint32_t used_h   = std::min(probes_y * dirs_sqrt, atlas_h);

    SDL_GPUStorageTextureReadWriteBinding rw = {};
    rw.texture = cascade_tex[c_write];
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
    SDL_BindGPUComputePipeline(pass, cascade_pipeline);

    SDL_GPUTextureSamplerBinding smps[3] = {
      { sdf_tex_,            sampler },
      { capture_tex,         sampler },
      { cascade_tex[c_read], sampler },
    };
    SDL_BindGPUComputeSamplers(pass, 0, smps, 3);
    SDL_PushGPUComputeUniformData(cmd, 0, &cp, sizeof(cp));
    SDL_DispatchGPUCompute(pass, groups(used_w, 16), groups(used_h, 16), 1);
    SDL_EndGPUComputePass(pass);
    std::swap(c_read, c_write);
  }

  {
    struct ResolveParams { int32_t rt_w, rt_h, _pad0, _pad1; } rp;
    static_assert(sizeof(ResolveParams) == 16, "ResolveParams must be 16 bytes");
    rp.rt_w  = (int32_t)rt_w;
    rp.rt_h  = (int32_t)rt_h;
    rp._pad0 = rp._pad1 = 0;

    SDL_GPUStorageTextureReadWriteBinding rw = {};
    rw.texture = fluence_tex;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, &rw, 1, nullptr, 0);
    SDL_BindGPUComputePipeline(pass, rc_resolve_pipeline);
    SDL_GPUTextureSamplerBinding smp = { cascade_tex[c_read], sampler };
    SDL_BindGPUComputeSamplers(pass, 0, &smp, 1);
    SDL_PushGPUComputeUniformData(cmd, 0, &rp, sizeof(rp));
    SDL_DispatchGPUCompute(pass, disp_x, disp_y, 1);
    SDL_EndGPUComputePass(pass);
  }
}

SDL_GPUTexture *RadianceCascades::fluence_texture() const {
  return ready() ? fluence_tex : nullptr;
}

SDL_GPUTexture *RadianceCascades::capture_texture() const {
  return ready() ? capture_tex : nullptr;
}

SDL_GPUTexture *RadianceCascades::sdf_texture() const {
  return ready() ? sdf_tex_ : nullptr;
}

SDL_GPUSampler *RadianceCascades::linear_sampler() const {
  return sampler;
}

SDL_GPUTextureFormat RadianceCascades::capture_depth_format() const {
  return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
}

uint32_t RadianceCascades::rt_width() const  { return rt_w; }
uint32_t RadianceCascades::rt_height() const { return rt_h; }

void RadianceCascades::cleanup(SDL_GPUDevice *device) {
  if (!gpu_device) return;
  release_targets();
  if (jfa_seed_pipeline)    { SDL_ReleaseGPUComputePipeline(device, jfa_seed_pipeline);    jfa_seed_pipeline = nullptr; }
  if (jfa_step_pipeline)    { SDL_ReleaseGPUComputePipeline(device, jfa_step_pipeline);    jfa_step_pipeline = nullptr; }
  if (sdf_resolve_pipeline) { SDL_ReleaseGPUComputePipeline(device, sdf_resolve_pipeline); sdf_resolve_pipeline = nullptr; }
  if (cascade_pipeline)     { SDL_ReleaseGPUComputePipeline(device, cascade_pipeline);     cascade_pipeline = nullptr; }
  if (rc_resolve_pipeline)  { SDL_ReleaseGPUComputePipeline(device, rc_resolve_pipeline);  rc_resolve_pipeline = nullptr; }
  if (sampler)              { SDL_ReleaseGPUSampler(device, sampler);                      sampler = nullptr; }
  gpu_device = nullptr;
  rt_w = rt_h = atlas_w = atlas_h = 0;
}
