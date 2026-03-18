#pragma once
#include "terrain/terrain_mesh.h"
#include "gpu/gpu.h"
#include "core/asset_manager.h"
#include <SDL3/SDL_gpu.h>
#include <flecs.h>
#include <glm/glm.hpp>
#include <vector>

class RigRenderer {
public:
    void init(SDL_GPUDevice *device,
              SDL_Window *window,
              SDL_GPUBuffer *dummy_ssbo,
              AssetManager *am,
              SDL_GPUTextureFormat depth_format);

    uint32_t prepare(SDL_GPUCommandBuffer *cmd, flecs::world &ecs);

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

    void emit_bone_oct(const glm::vec3 &a, const glm::vec3 &b,
                       float width, glm::vec3 color,
                       std::vector<BasaltVertex> &out_verts);

    void emit_sphere(const glm::vec3 &center, float radius, glm::vec3 color,
                     int sectors, int rings,
                     std::vector<BasaltVertex> &out_verts);

    void emit_tripod(const glm::vec3 &center, float size,
                     std::vector<BasaltVertex> &out_verts);

    void emit_box(const glm::vec3 &center, float half_size, glm::vec3 color,
                  std::vector<BasaltVertex> &out_verts);

    void emit_wireframe_box(const glm::vec3 &center, float half_size,
                            float edge_radius, glm::vec3 color,
                            std::vector<BasaltVertex> &out_verts);

    void emit_diamond(const glm::vec3 &center, float radius, glm::vec3 color,
                      std::vector<BasaltVertex> &out_verts);

    void emit_flat_circle(const glm::vec3 &center, float radius, glm::vec3 color,
                          int segments, std::vector<BasaltVertex> &out_verts);

    static constexpr uint32_t MAX_RIG_VERTICES = 65536;

    bool                     initialized   = false;
    SDL_GPUGraphicsPipeline *pipeline      = nullptr;
    SDL_GPUBuffer           *dummy_ssbo_   = nullptr;
    SDL_GPUDevice           *gpu_device    = nullptr;
    AssetManager            *asset_manager = nullptr;

    SDL_GPUBuffer         *rig_vbo       = nullptr;
    SDL_GPUTransferBuffer *transfer_buf  = nullptr;
};
