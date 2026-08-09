#pragma once
#include <SDL3/SDL_gpu.h>
#include <string>
#include <unordered_map>
#include <cstdint>

struct ShaderAsset {
    SDL_GPUShader      *shader               = nullptr;
    std::string         path;
    uint64_t            last_mtime           = 0;
    SDL_GPUShaderStage  stage;
    int                 num_uniform_buffers  = 0;
    int                 num_storage_buffers  = 0;
    int                 num_sampler_textures = 0;
    bool                is_compute           = false;
};

struct PipelineRecord {
    std::string vert_shader_key;
    std::string frag_shader_key;
    bool        needs_rebuild = false;
};

class AssetManager {
public:
    void init(SDL_GPUDevice *device);

    SDL_GPUShader *load_shader(const std::string &key,
                               const std::string &path,
                               SDL_GPUShaderStage stage,
                               int num_uniform_buffers  = 0,
                               int num_storage_buffers  = 0,
                               int num_sampler_textures = 0);

    SDL_GPUShader *load_compute_shader(const std::string &key,
                                       const std::string &path,
                                       int num_uniform_buffers      = 0,
                                       int num_rw_storage_buffers   = 0,
                                       int num_ro_storage_buffers   = 0);

    void register_pipeline(const std::string &key,
                           const std::string &vert_key,
                           const std::string &frag_key);

    void register_compute_pipeline(const std::string &key,
                                   const std::string &shader_key);

    void check_for_updates();

    bool pipeline_needs_rebuild(const std::string &key) const;
    void clear_rebuild_flag(const std::string &key);

    void register_buffer(const std::string &key, SDL_GPUBuffer *buffer);
    void release_buffer(const std::string &key);

    void clear();

    void render_debug_ui() const;

private:
    SDL_GPUDevice *device = nullptr;

    std::unordered_map<std::string, ShaderAsset>     shader_cache;
    std::unordered_map<std::string, PipelineRecord>  pipeline_registry;
    std::unordered_map<std::string, SDL_GPUBuffer *> buffer_registry;

    static uint64_t    get_mtime(const std::string &path);
    SDL_GPUShader     *create_shader_internal(const ShaderAsset &meta);
};
