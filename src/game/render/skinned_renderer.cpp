#include "skinned_renderer.h"
#include "../../engine/gpu/gpu.h"
#include "../config.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <vector>

#ifdef SHADER_DIR
static const char *s_shader_dir = SHADER_DIR;
#else
static const char *s_shader_dir = "shaders";
#endif

bool SkinnedRenderer::build_pipeline(SDL_Window *window) {
    if (!device_ || !assets_) return false;

    std::string shader_dir = s_shader_dir;
    SDL_GPUTextureFormat sc_fmt = SDL_GetGPUSwapchainTextureFormat(device_, window);

    // vert: 1 uniform (SceneUniforms), 1 storage (BoneBuffer)
    // frag: 1 uniform (SceneUniforms), 1 storage (lights)
    SDL_GPUShader *vert = assets_->load_shader(
        "skinned_char.vert",
        shader_dir + "/skinned_character.vert.glsl.spv",
        SDL_GPU_SHADERSTAGE_VERTEX, 1, 1);
    SDL_GPUShader *frag = assets_->load_shader(
        "skinned_char.frag",
        shader_dir + "/skinned_character.frag.glsl.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

    if (!vert || !frag) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedRenderer: failed to load shaders");
        return false;
    }

    SDL_GPUVertexBufferDescription vbuf = {};
    vbuf.slot       = 0;
    vbuf.pitch      = (Uint32)sizeof(SkinnedVertex);
    vbuf.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[6] = {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(SkinnedVertex, position)  };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(SkinnedVertex, normal)    };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(SkinnedVertex, texcoord)  };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (Uint32)offsetof(SkinnedVertex, tangent)   };
    attrs[4] = { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, (Uint32)offsetof(SkinnedVertex, joints)    };
    attrs[5] = { 5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (Uint32)offsetof(SkinnedVertex, weights)   };

    SDL_GPUColorTargetDescription color_desc = {};
    color_desc.format = sc_fmt;

    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader   = vert;
    pi.fragment_shader = frag;
    pi.vertex_input_state.vertex_buffer_descriptions = &vbuf;
    pi.vertex_input_state.num_vertex_buffers         = 1;
    pi.vertex_input_state.vertex_attributes          = attrs;
    pi.vertex_input_state.num_vertex_attributes      = 6;
    pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.cull_mode                    = SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions         = &color_desc;
    pi.target_info.num_color_targets                 = 1;
    pi.target_info.has_depth_stencil_target          = true;
    pi.target_info.depth_stencil_format              = depth_format_;
    pi.depth_stencil_state.compare_op                = SDL_GPU_COMPAREOP_LESS;
    pi.depth_stencil_state.enable_depth_test         = true;
    pi.depth_stencil_state.enable_depth_write        = true;

    if (pipeline_) {
        SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        pipeline_ = nullptr;
    }
    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pi);
    if (!pipeline_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SkinnedRenderer: pipeline creation failed: %s", SDL_GetError());
        return false;
    }

    assets_->register_pipeline("skinned_char", "skinned_char.vert", "skinned_char.frag");
    SDL_Log("SkinnedRenderer: pipeline created (stride=%zu)", sizeof(SkinnedVertex));
    return true;
}

void SkinnedRenderer::init(SDL_GPUDevice *device, SDL_Window *window, AssetManager *assets) {
    if (initialized_) return;
    device_ = device;
    assets_ = assets;

    if (SDL_GPUTextureSupportsFormat(device,
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        depth_format_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    } else {
        depth_format_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    }

    if (!build_pipeline(window)) return;

    SDL_GPUBufferCreateInfo bi = {};
    bi.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bi.size  = (Uint32)sizeof(BonePalette);
    bone_ssbo_ = SDL_CreateGPUBuffer(device, &bi);
    if (!bone_ssbo_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedRenderer: failed to create bone SSBO");
        return;
    }

    SDL_GPUTransferBufferCreateInfo tbi = {};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size  = (Uint32)sizeof(BonePalette);
    bone_transfer_ = SDL_CreateGPUTransferBuffer(device, &tbi);
    if (!bone_transfer_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedRenderer: failed to create bone transfer buf");
        SDL_ReleaseGPUBuffer(device, bone_ssbo_);
        bone_ssbo_ = nullptr;
        return;
    }

    initialized_ = true;
    SDL_Log("SkinnedRenderer: initialized");
}

void SkinnedRenderer::cleanup() {
    if (!initialized_) return;
    if (pipeline_)      { SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_); pipeline_ = nullptr; }
    if (vbo_)           { SDL_ReleaseGPUBuffer(device_, vbo_); vbo_ = nullptr; }
    if (ibo_)           { SDL_ReleaseGPUBuffer(device_, ibo_); ibo_ = nullptr; }
    if (bone_ssbo_)     { SDL_ReleaseGPUBuffer(device_, bone_ssbo_); bone_ssbo_ = nullptr; }
    if (bone_transfer_) { SDL_ReleaseGPUTransferBuffer(device_, bone_transfer_); bone_transfer_ = nullptr; }
    initialized_ = false;
    char_loaded_ = false;
}

void SkinnedRenderer::load_character(const std::string &path) {
    if (!initialized_) return;

    GltfSkinnedAsset asset = load_gltf_skinned(path);
    if (!asset.ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SkinnedRenderer: failed to load '%s': %s",
                     path.c_str(), asset.error.c_str());
        return;
    }
    if (asset.meshes.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SkinnedRenderer: no meshes in '%s'", path.c_str());
        return;
    }

    skeleton_ = asset.skeleton;

    std::vector<SkinnedVertex> all_verts;
    std::vector<uint32_t>      all_idx;
    for (auto &mesh : asset.meshes) {
        uint32_t base = (uint32_t)all_verts.size();
        all_verts.insert(all_verts.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (uint32_t idx : mesh.indices)
            all_idx.push_back(base + idx);
    }
    index_count_ = (uint32_t)all_idx.size();

    if (vbo_) { SDL_ReleaseGPUBuffer(device_, vbo_); vbo_ = nullptr; }
    if (ibo_) { SDL_ReleaseGPUBuffer(device_, ibo_); ibo_ = nullptr; }

    vbo_ = gpu_upload_buffer(device_, all_verts.data(),
                              (uint32_t)(all_verts.size() * sizeof(SkinnedVertex)),
                              SDL_GPU_BUFFERUSAGE_VERTEX);
    ibo_ = gpu_upload_buffer(device_, all_idx.data(),
                              (uint32_t)(all_idx.size() * sizeof(uint32_t)),
                              SDL_GPU_BUFFERUSAGE_INDEX);

    if (!vbo_ || !ibo_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedRenderer: VBO/IBO upload failed");
        return;
    }

    for (auto &clip : asset.animations)
        clips_[clip.name] = std::move(clip);

    // Start with idle if available
    if (clips_.count("idle")) {
        player_.set_clip(&clips_["idle"]);
        current_clip_ = "idle";
    } else if (!clips_.empty()) {
        auto it = clips_.begin();
        player_.set_clip(&it->second);
        current_clip_ = it->first;
    }

    char_loaded_ = true;
    SDL_Log("SkinnedRenderer: loaded '%s' (%u verts, %u indices, %zu bones)",
            path.c_str(), (uint32_t)all_verts.size(), index_count_,
            skeleton_.bones.size());
}

void SkinnedRenderer::load_animation(const std::string &name, const std::string &path) {
    GltfSkinnedAsset asset = load_gltf_skinned(path);
    if (!asset.ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SkinnedRenderer: failed to load anim '%s': %s",
                     path.c_str(), asset.error.c_str());
        return;
    }
    for (auto &clip : asset.animations)
        clips_[name] = std::move(clip);
    SDL_Log("SkinnedRenderer: loaded animation '%s'", name.c_str());
}

void SkinnedRenderer::set_animation(const std::string &name) {
    auto it = clips_.find(name);
    if (it == clips_.end()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SkinnedRenderer: animation '%s' not found", name.c_str());
        return;
    }
    if (current_clip_ != name) {
        player_.set_clip(&it->second);
        current_clip_ = name;
    }
}

void SkinnedRenderer::update(float dt, const glm::vec3 &player_pos, float facing, float speed) {
    if (!initialized_ || !char_loaded_) return;

    // Select clip by speed
    if (speed < 0.1f)      set_animation("idle");
    else if (speed < 5.0f) set_animation("walk");
    else                    set_animation("run");

    player_.update(dt);

    std::vector<BoneLocalTransform> locals;
    player_.sample(locals);

    // Root: translate * rotateZ(facing) * rotateX(-90deg, Y-up->Z-up) * scale(HEX_SIZE, Blender meters->terrain units)
    glm::mat4 root = glm::translate(glm::mat4(1.f), player_pos)
                   * glm::rotate(glm::mat4(1.f), facing, glm::vec3(0.f, 0.f, 1.f))
                   * glm::rotate(glm::mat4(1.f), glm::radians(-90.0f), glm::vec3(1.f, 0.f, 0.f))
                   * glm::scale(glm::mat4(1.f), glm::vec3(Config::HEX_SIZE));

    palette_ = compute_bone_palette(skeleton_, locals, root);
}

void SkinnedRenderer::prepare(SDL_GPUCommandBuffer *cmd) {
    if (!initialized_ || !char_loaded_ || !bone_transfer_ || !bone_ssbo_) return;

    void *mapped = SDL_MapGPUTransferBuffer(device_, bone_transfer_, true);
    if (!mapped) return;
    std::memcpy(mapped, &palette_, sizeof(BonePalette));
    SDL_UnmapGPUTransferBuffer(device_, bone_transfer_);

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = bone_transfer_;
    src.offset          = 0;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = bone_ssbo_;
    dst.offset = 0;
    dst.size   = (Uint32)sizeof(BonePalette);
    SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    SDL_EndGPUCopyPass(cp);
}

void SkinnedRenderer::draw(SDL_GPURenderPass *pass,
                            SDL_GPUCommandBuffer *cmd,
                            const SceneUniforms &uniforms,
                            SDL_GPUBuffer *lights_ssbo,
                            SDL_GPUBuffer *clusters_ssbo,
                            SDL_GPUBuffer *light_indices_ssbo) {
    if (!initialized_ || !char_loaded_ || !vbo_ || !ibo_) return;
    if (!pipeline_) { SDL_Log("SkinnedRenderer: pipeline null, skipping draw"); return; }
    (void)clusters_ssbo;
    (void)light_indices_ssbo;

    SDL_BindGPUGraphicsPipeline(pass, pipeline_);

    // Vertex storage slot 0: bone SSBO
    SDL_BindGPUVertexStorageBuffers(pass, 0, &bone_ssbo_, 1);

    // Fragment storage slot 0: lights
    SDL_BindGPUFragmentStorageBuffers(pass, 0, &lights_ssbo, 1);

    // Uniform slot 0: SceneUniforms (vertex + fragment)
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));

    SDL_GPUBufferBinding vb = { vbo_, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_GPUBufferBinding ib = { ibo_, 0 };
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    SDL_DrawGPUIndexedPrimitives(pass, index_count_, 1, 0, 0, 0);
}