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

void AssetManager::register_compute_pipeline(const std::string &key,
                                              const std::string &shader_key) {
    PipelineRecord &rec = pipeline_registry[key];
    rec.vert_shader_key = shader_key;
    rec.frag_shader_key = "";
    rec.needs_rebuild   = false;
}

void AssetManager::check_for_updates() {
    for (auto &[key, asset] : shader_cache) {
        uint64_t mtime = get_mtime(asset.path);
        if (mtime == 0 || mtime <= asset.last_mtime) continue;

        // File changed — reload shader (graphics only; compute pipelines are
        // rebuilt by the caller using the path).
        if (asset.shader) {
            SDL_ReleaseGPUShader(device, asset.shader);
            asset.shader = nullptr;
        }
        asset.last_mtime = mtime;
        asset.dirty      = true;

        if (asset.stage != SDL_GPU_SHADERSTAGE_VERTEX || asset.num_storage_buffers >= 0) {
            // Rebuild the shader object if it was a graphics shader
            // (compute shaders don't have an SDL_GPUShader object here)
        }
        asset.shader = create_shader_internal(asset);

        SDL_Log("AssetManager: Hot-reloaded shader '%s' (%s)", key.c_str(), asset.path.c_str());

        // Mark dependent pipelines
        for (auto &[pkey, prec] : pipeline_registry) {
            if (prec.vert_shader_key == key || prec.frag_shader_key == key) {
                prec.needs_rebuild = true;
                SDL_Log("AssetManager: Pipeline '%s' flagged for rebuild", pkey.c_str());
            }
        }
    }

    // Clear per-frame dirty flags after propagation
    for (auto &[key, asset] : shader_cache)
        asset.dirty = false;
}

bool AssetManager::pipeline_needs_rebuild(const std::string &key) const {
    auto it = pipeline_registry.find(key);
    return it != pipeline_registry.end() && it->second.needs_rebuild;
}

void AssetManager::clear_rebuild_flag(const std::string &key) {
    auto it = pipeline_registry.find(key);
    if (it != pipeline_registry.end())
        it->second.needs_rebuild = false;
}

void AssetManager::register_buffer(const std::string &key, SDL_GPUBuffer *buffer) {
    auto it = buffer_registry.find(key);
    if (it != buffer_registry.end() && it->second) {
        SDL_ReleaseGPUBuffer(device, it->second);
    }
    buffer_registry[key] = buffer;
}

SDL_GPUBuffer *AssetManager::get_buffer(const std::string &key) const {
    auto it = buffer_registry.find(key);
    return it != buffer_registry.end() ? it->second : nullptr;
}

void AssetManager::release_buffer(const std::string &key) {
    auto it = buffer_registry.find(key);
    if (it != buffer_registry.end()) {
        if (it->second) SDL_ReleaseGPUBuffer(device, it->second);
        buffer_registry.erase(it);
    }
}

void AssetManager::clear() {
    for (auto &[key, asset] : shader_cache) {
        if (asset.shader) SDL_ReleaseGPUShader(device, asset.shader);
    }
    shader_cache.clear();

    for (auto &[key, buf] : buffer_registry) {
        if (buf) SDL_ReleaseGPUBuffer(device, buf);
    }
    buffer_registry.clear();

    pipeline_registry.clear();
}

void AssetManager::render_debug_ui() const {
    if (ImGui::BeginTable("##shaders", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, 160))) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Path");
        ImGui::TableSetupColumn("MTime");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();
        for (auto &[key, asset] : shader_cache) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(key.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(asset.path.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%llu", (unsigned long long)asset.last_mtime);
            ImGui::TableSetColumnIndex(3);
            if (asset.shader == nullptr && asset.stage == SDL_GPU_SHADERSTAGE_VERTEX
                    && asset.num_storage_buffers > 0)
                ImGui::TextUnformatted("compute");
            else if (!asset.shader)
                ImGui::TextColored({1,0.3f,0.3f,1}, "ERROR");
            else
                ImGui::TextColored({0.3f,1,0.3f,1}, "OK");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Pipelines:");
    if (ImGui::BeginTable("##pipelines", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, 100))) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Vert/Shader");
        ImGui::TableSetupColumn("Frag");
        ImGui::TableSetupColumn("Rebuild?");
        ImGui::TableHeadersRow();
        for (auto &[key, rec] : pipeline_registry) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(key.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(rec.vert_shader_key.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(rec.frag_shader_key.c_str());
            ImGui::TableSetColumnIndex(3);
            if (rec.needs_rebuild)
                ImGui::TextColored({1,1,0,1}, "YES");
            else
                ImGui::TextUnformatted("no");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Tracked Buffers:");
    if (ImGui::BeginTable("##buffers", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, 80))) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Pointer");
        ImGui::TableHeadersRow();
        for (auto &[key, buf] : buffer_registry) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(key.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%p", (void*)buf);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Force Reload All Shaders")) {
        // Zero out mtimes so check_for_updates() picks them all up next frame.
        // cast away const for the button action — acceptable debug utility.
        auto *self = const_cast<AssetManager*>(this);
        for (auto &[key, asset] : self->shader_cache)
            asset.last_mtime = 0;
    }
}
