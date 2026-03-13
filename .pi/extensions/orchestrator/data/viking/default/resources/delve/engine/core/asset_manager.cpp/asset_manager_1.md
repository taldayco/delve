#include "asset_manager.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <sys/stat.h>
#include <vector>
#include <cstdio>

// ---- mtime ---------------------------------------------------------------

uint64_t AssetManager::get_mtime(const std::string &path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return 0;
    return (uint64_t)st.st_mtime;
}

// ---- Internal shader creation --------------------------------------------

SDL_GPUShader *AssetManager::create_shader_internal(const ShaderAsset &meta) {
    SDL_IOStream *io = SDL_IOFromFile(meta.path.c_str(), "rb");
    if (!io) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetManager: Failed to open shader: %s", meta.path.c_str());
        return nullptr;
    }
    Sint64 size = SDL_GetIOSize(io);
    if (size <= 0) { SDL_CloseIO(io); return nullptr; }
    std::vector<uint8_t> code(size);
    SDL_ReadIO(io, code.data(), size);
    SDL_CloseIO(io);

    SDL_GPUShaderCreateInfo info = {};
    info.code                = code.data();
    info.code_size           = (size_t)size;
    info.entrypoint          = "main";
    info.format              = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage               = meta.stage;
    info.num_uniform_buffers = (Uint32)meta.num_uniform_buffers;
    info.num_storage_buffers = (Uint32)meta.num_storage_buffers;
    info.num_samplers        = (Uint32)meta.num_sampler_textures;

    SDL_GPUShader *shader = SDL_CreateGPUShader(device, &info);
    if (!shader)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetManager: Failed to create shader %s: %s",
                     meta.path.c_str(), SDL_GetError());
    return shader;
}

// ---- Public API ----------------------------------------------------------

void AssetManager::init(SDL_GPUDevice *dev) {
    device = dev;
}

SDL_GPUShader *AssetManager::load_shader(const std::string &key,
                                          const std::string &path,
                                          SDL_GPUShaderStage stage,
                                          int num_uniform_buffers,
                                          int num_storage_buffers,
                                          int num_sampler_textures) {
    auto it = shader_cache.find(key);
    if (it != shader_cache.end())
        return it->second.shader;

    ShaderAsset meta;
    meta.path                = path;
    meta.stage               = stage;
    meta.num_uniform_buffers = num_uniform_buffers;
    meta.num_storage_buffers = num_storage_buffers;
    meta.num_sampler_textures = num_sampler_textures;
    meta.last_mtime          = get_mtime(path);
    meta.dirty               = false;
    meta.shader              = create_shader_internal(meta);

    shader_cache[key] = meta;
    return meta.shader;
}

SDL_GPUShader *AssetManager::load_compute_shader(const std::string &key,
                                                   const std::string &path,
                                                   int num_uniform_buffers,
                                                   int num_rw_storage_buffers,
                                                   int num_ro_storage_buffers) {
    auto it = shader_cache.find(key);
    if (it != shader_cache.end())
        return it->second.shader;

    // Compute shaders use VERTEX stage slot as a placeholder; the distinction
    // is handled by SDL_CreateGPUComputePipeline, not the shader object itself.
    // We store rw+ro in num_storage_buffers for display purposes only.
    ShaderAsset meta;
    meta.path                = path;
    meta.stage               = SDL_GPU_SHADERSTAGE_VERTEX; // not used for compute
    meta.num_uniform_buffers = num_uniform_buffers;
    meta.num_storage_buffers = num_rw_storage_buffers + num_ro_storage_buffers;
    meta.num_sampler_textures = 0;
    meta.last_mtime          = get_mtime(path);
    meta.dirty               = false;

    // For compute shaders we don't create an SDL_GPUShader — callers use the
    // path directly to build SDL_GPUComputePipeline.  We still track the file
    // for hot-swap detection.
    meta.shader = nullptr;

    shader_cache[key] = meta;
    return nullptr; // compute pipeline callers use the path, not a shader handle
}

void AssetManager::register_pipeline(const std::string &key,
                                      const std::string &vert_key,
                                      const std::string &frag_key) {
    PipelineRecord &rec = pipeline_registry[key];
    rec.vert_shader_key = vert_key;
    rec.frag_shader_key = frag_key;
    rec.needs_rebuild   = false;
}