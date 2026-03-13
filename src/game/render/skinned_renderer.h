#pragma once
#include "skeletal_animation.h"
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
    void init(SDL_GPUDevice *device, SDL_Window *window, AssetManager *assets);
    void cleanup();

    void load_character(const std::string &path);
    void load_animation(const std::string &name, const std::string &path);
    void set_animation(const std::string &name);

    // player_pos/facing/speed drive clip selection and root transform
    void update(float dt, const glm::vec3 &player_pos, float facing, float speed);
    void prepare(SDL_GPUCommandBuffer *cmd);
    void draw(SDL_GPURenderPass *pass,
              SDL_GPUCommandBuffer *cmd,
              const SceneUniforms &uniforms,
              SDL_GPUBuffer *lights_ssbo,
              SDL_GPUBuffer *clusters_ssbo,
              SDL_GPUBuffer *light_indices_ssbo);

    bool is_initialized() const { return initialized_; }
    bool has_character()  const { return char_loaded_; }

    float debug_uniform_scale = 1.0f;
    bool  debug_raw_transform = false;

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
    AnimationPlayer                                    player_;
    BonePalette                                        palette_{};

    // current clip name for hysteresis
    std::string current_clip_;

    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    bool initialized_  = false;
    bool char_loaded_  = false;
    bool printed_root_ = false;
};