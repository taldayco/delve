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