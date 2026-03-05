#pragma once
#include "terrain/terrain_mesh.h"   // BasaltVertex, SceneUniforms
#include "gpu/gpu.h"                // UploadManager
#include "core/asset_manager.h"
#include <SDL3/SDL_gpu.h>
#include <flecs.h>
#include <glm/glm.hpp>
#include <vector>

class RigRenderer {
public:
    // terrain_pipeline and dummy_ssbo are borrowed (not owned) from TerrainRenderer.
    // dummy_ssbo fills binding slots 1 and 2 (pipeline declares num_storage_buffers=3).
    void init(SDL_GPUDevice *device,
              SDL_GPUGraphicsPipeline *terrain_pipeline,
              SDL_GPUBuffer *dummy_ssbo,
              AssetManager *am);

    // Call BEFORE the rig render pass is opened.
    // Builds geometry from ECS, uploads to rig_vbo via a copy pass.
    // Returns the number of vertices to draw (0 if nothing to draw).
    uint32_t prepare(SDL_GPUCommandBuffer *cmd, flecs::world &ecs);

    // Call inside the rig render pass.
    void draw(SDL_GPURenderPass *pass,
              SDL_GPUCommandBuffer *cmd,
              const SceneUniforms &uniforms,
              SDL_GPUBuffer *point_light_ssbo,
              uint32_t vertex_count);

    void cleanup(SDL_GPUDevice *device);

    bool is_initialized() const { return initialized; }

private:
    void emit_cylinder(const glm::vec3 &a, const glm::vec3 &b,
                       float radius, glm::vec3 color, int sides,
                       std::vector<BasaltVertex> &out_verts);

    void emit_box(const glm::vec3 &center, float half_size, glm::vec3 color,
                  std::vector<BasaltVertex> &out_verts);

    void emit_diamond(const glm::vec3 &center, float radius, glm::vec3 color,
                      std::vector<BasaltVertex> &out_verts);

    static constexpr uint32_t MAX_RIG_VERTICES = 65536; // ~2.5 MB

    bool                     initialized   = false;
    SDL_GPUGraphicsPipeline *pipeline      = nullptr; // borrowed
    SDL_GPUBuffer           *dummy_ssbo_   = nullptr; // borrowed
    SDL_GPUDevice           *gpu_device    = nullptr;
    AssetManager            *asset_manager = nullptr;

    SDL_GPUBuffer         *rig_vbo       = nullptr; // owned, static-sized
    SDL_GPUTransferBuffer *transfer_buf  = nullptr; // owned, persistent mapped staging
};
