#pragma once
#include "skeletal_animation.h"
#include "../rig.h"
#include "../../engine/core/gltf_loader.h"
#include "../../engine/core/asset_manager.h"
#include "../../engine/gpu/gpu.h"
#include "../terrain/terrain_mesh.h"
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

class SkinnedRenderer {
public:
    void init(SDL_GPUDevice *device, SDL_Window *window, AssetManager *assets,
              SDL_GPUTextureFormat depth_format);
    void cleanup();

    void load_character(const std::string &path);
    void load_animation(const std::string &name, const std::string &path);
    void set_animation(const std::string &name);

    void prepare(SDL_GPUCommandBuffer *cmd);
    void draw(SDL_GPURenderPass *pass,
              SDL_GPUCommandBuffer *cmd,
              const SceneUniforms &uniforms,
              SDL_GPUBuffer *lights_ssbo,
              SDL_GPUBuffer *light_grid_ssbo,
              SDL_GPUBuffer *light_indices_ssbo,
              SDL_GPUTexture *light_tex,
              SDL_GPUSampler *light_smp,
              SDL_GPUTexture *fluence_tex,
              SDL_GPUSampler *fluence_smp);

    bool is_initialized() const { return initialized_; }
    bool has_character()  const { return char_loaded_; }

    void select_clip(const std::string &name, float crossfade_duration = 0.25f);
    void update_mixer(float dt);
    void sample_pose(SkinnedPose &out) const;
    void set_playback_speed(float speed);
    void set_bone_palette(const BonePalette &palette);

    const GltfSkeleton &get_skeleton() const { return skeleton_; }
    const BoneMap      &get_bone_map() const { return bone_map_; }

    float debug_uniform_scale = 1.0f;

private:
    bool build_pipeline(SDL_Window *window);

    SDL_GPUDevice           *device_        = nullptr;
    AssetManager            *assets_        = nullptr;
    SDL_GPUGraphicsPipeline *pipeline_      = nullptr;
    SDL_GPUBuffer           *vbo_           = nullptr;
    SDL_GPUBuffer           *ibo_           = nullptr;
    SDL_GPUBuffer           *bone_ssbo_     = nullptr;
    SDL_GPUTransferBuffer   *bone_transfer_ = nullptr;
    uint32_t                 index_count_   = 0;

    GltfSkeleton                                       skeleton_;
    std::unordered_map<std::string, GltfAnimationClip> clips_;
    AnimationMixer                                     mixer_;
    BonePalette                                        palette_{};
    BoneMap                                            bone_map_;

    std::string current_clip_;

    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    bool initialized_  = false;
    bool char_loaded_  = false;
};
