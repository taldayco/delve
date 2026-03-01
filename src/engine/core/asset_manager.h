#pragma once
#include <SDL3/SDL_gpu.h>
#include <string>
#include <unordered_map>
#include <cstdint>

// --- Shader Asset ---
struct ShaderAsset {
    SDL_GPUShader      *shader               = nullptr;
    std::string         path;
    uint64_t            last_mtime           = 0;
    SDL_GPUShaderStage  stage;
    int                 num_uniform_buffers  = 0;
    int                 num_storage_buffers  = 0;
    int                 num_sampler_textures = 0;
    bool                dirty                = false;
};

// --- Pipeline Dependency Record ---
struct PipelineRecord {
    std::string vert_shader_key;   // key into shader_cache (or compute shader key)
    std::string frag_shader_key;   // empty for compute pipelines
    bool        needs_rebuild = false;
};

// --- AssetManager ---
class AssetManager {
public:
    // Must be called once after gpu_init; stores device for all GPU operations.
    void init(SDL_GPUDevice *device);

    // Load (or return cached) a compiled SPIR-V graphics shader.
    SDL_GPUShader *load_shader(const std::string &key,
                               const std::string &path,
                               SDL_GPUShaderStage stage,
                               int num_uniform_buffers  = 0,
                               int num_storage_buffers  = 0,
                               int num_sampler_textures = 0);

    // Register a compute shader path for hot-swap tracking (returns nullptr —
    // compute pipelines are built separately via build_compute_pipeline()).
    SDL_GPUShader *load_compute_shader(const std::string &key,
                                       const std::string &path,
                                       int num_uniform_buffers      = 0,
                                       int num_rw_storage_buffers   = 0,
                                       int num_ro_storage_buffers   = 0);

    // Register a graphics pipeline so dirty shaders trigger a rebuild flag.
    void register_pipeline(const std::string &key,
                           const std::string &vert_key,
                           const std::string &frag_key);

    // Register a compute pipeline with its single shader dependency.
    void register_compute_pipeline(const std::string &key,
                                   const std::string &shader_key);

    // Call once per frame before rendering. Checks mtime of all tracked shader
    // files; reloads changed shaders and sets needs_rebuild on dependent pipelines.
    void check_for_updates();

    bool pipeline_needs_rebuild(const std::string &key) const;
    void clear_rebuild_flag(const std::string &key);

    // Centralized GPU buffer tracking (ownership transferred to manager).
    void register_buffer(const std::string &key, SDL_GPUBuffer *buffer);
    SDL_GPUBuffer *get_buffer(const std::string &key) const;
    void release_buffer(const std::string &key);

    // Release all managed GPU resources. Call before SDL_DestroyGPUDevice.
    void clear();

    // ImGui debug panel: shaders, pipelines, buffers.
    void render_debug_ui() const;

private:
    SDL_GPUDevice *device = nullptr;

    std::unordered_map<std::string, ShaderAsset>     shader_cache;
    std::unordered_map<std::string, PipelineRecord>  pipeline_registry;
    std::unordered_map<std::string, SDL_GPUBuffer *> buffer_registry;

    static uint64_t    get_mtime(const std::string &path);
    SDL_GPUShader     *create_shader_internal(const ShaderAsset &meta);
};
