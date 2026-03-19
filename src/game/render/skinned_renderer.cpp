#include "skinned_renderer.h"
#include "../rig.h"
#include "../../engine/gpu/gpu.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
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

    SDL_GPUShader *vert = assets_->load_shader(
        "skinned_char.vert",
        shader_dir + "/skinned_character.vert.glsl.spv",
        SDL_GPU_SHADERSTAGE_VERTEX, 1, 1);
    SDL_GPUShader *frag = assets_->load_shader(
        "skinned_char.frag",
        shader_dir + "/skinned_character.frag.glsl.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 3);

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
    pi.depth_stencil_state.compare_op                = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
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

void SkinnedRenderer::init(SDL_GPUDevice *device, SDL_Window *window, AssetManager *assets,
                           SDL_GPUTextureFormat depth_format) {
    if (initialized_) return;
    device_ = device;
    assets_ = assets;
    depth_format_ = depth_format;

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

    if (clips_.count("idle")) {
        mixer_.set_clip(&clips_["idle"]);
        current_clip_ = "idle";
    } else if (!clips_.empty()) {
        auto it = clips_.begin();
        mixer_.set_clip(&it->second);
        current_clip_ = it->first;
    }

    bone_map_ = BoneMap::build_from_skeleton(skeleton_);
    SDL_Log("SkinnedRenderer: BoneMap — hips=%d spine=%d chest=%d neck=%d head=%d",
            bone_map_.hips, bone_map_.spine, bone_map_.chest, bone_map_.neck, bone_map_.head);

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
    std::unordered_map<std::string, int> char_bone_map;
    for (int i = 0; i < (int)skeleton_.bones.size(); ++i)
        char_bone_map[skeleton_.bones[i].name] = i;

    const auto &anim_bones = asset.skeleton.bones;

    for (auto &clip : asset.animations) {
        std::vector<GltfAnimChannel> remapped;
        remapped.reserve(clip.channels.size());
        for (auto &ch : clip.channels) {
            if (ch.bone_index < 0 || ch.bone_index >= (int)anim_bones.size())
                continue;
            const std::string &bone_name = anim_bones[ch.bone_index].name;
            auto it = char_bone_map.find(bone_name);
            if (it == char_bone_map.end())
                continue;
            ch.bone_index = it->second;
            remapped.push_back(std::move(ch));
        }
        clip.channels = std::move(remapped);
        clips_[name] = std::move(clip);
    }
    SDL_Log("SkinnedRenderer: loaded animation '%s' (%zu channels remapped)",
            name.c_str(), clips_.count(name) ? clips_[name].channels.size() : 0);
}

void SkinnedRenderer::set_animation(const std::string &name) {
    auto it = clips_.find(name);
    if (it == clips_.end()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SkinnedRenderer: animation '%s' not found", name.c_str());
        return;
    }
    if (current_clip_ != name) {
        mixer_.set_clip(&it->second);
        current_clip_ = name;
    }
}

void SkinnedRenderer::select_clip(const std::string &name, float crossfade_duration) {
    auto it = clips_.find(name);
    if (it == clips_.end()) return;
    if (current_clip_ != name) {
        mixer_.set_clip(&it->second, crossfade_duration);
        current_clip_ = name;
    }
}

void SkinnedRenderer::update_mixer(float dt) {
    if (!initialized_ || !char_loaded_) return;
    mixer_.update(dt);
}

void SkinnedRenderer::sample_pose(SkinnedPose &out) const {
    if (!initialized_ || !char_loaded_) return;
    int num_bones = std::min((int)skeleton_.bones.size(), 65);
    out.local_transforms.resize(num_bones);
    mixer_.sample(skeleton_, out.local_transforms);
}

void SkinnedRenderer::set_playback_speed(float speed) {
    mixer_.set_playback_speed(speed);
}

void SkinnedRenderer::set_bone_palette(const BonePalette &palette) {
    palette_ = palette;
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
                            SDL_GPUBuffer *light_grid_ssbo,
                            SDL_GPUBuffer *light_indices_ssbo) {
    if (!initialized_ || !char_loaded_ || !vbo_ || !ibo_) return;
    if (!pipeline_) { SDL_Log("SkinnedRenderer: pipeline null, skipping draw"); return; }

    SDL_BindGPUGraphicsPipeline(pass, pipeline_);

    SDL_BindGPUVertexStorageBuffers(pass, 0, &bone_ssbo_, 1);

    SDL_GPUBuffer *frag_ssbos[3] = { lights_ssbo, light_grid_ssbo, light_indices_ssbo };
    SDL_BindGPUFragmentStorageBuffers(pass, 0, frag_ssbos, 3);

    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));

    SDL_GPUBufferBinding vb = { vbo_, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_GPUBufferBinding ib = { ibo_, 0 };
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    SDL_DrawGPUIndexedPrimitives(pass, index_count_, 1, 0, 0, 0);
}