#pragma once
#include "terrain/terrain_mesh.h"   // BasaltVertex, SceneUniforms
#include "gpu/gpu.h"                // UploadManager
#include "core/asset_manager.h"
#include "core/skinned_mesh.h"
#include "core/animator.h"
#include "core/gltf_loader.h"
#include "joint_mapping.h"
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

    // Skinned mesh pipeline
    bool use_skinned_mesh = false;

    bool init_skinned(SDL_GPUDevice *device, SDL_Window *window,
                      SDL_GPUTextureFormat depth_format,
                      AssetManager *am, const std::string &glb_path);

    bool prepare_skinned(SDL_GPUCommandBuffer *cmd, flecs::world &ecs);

    void draw_skinned(SDL_GPURenderPass *pass, SDL_GPUCommandBuffer *cmd,
                      const SceneUniforms &uniforms,
                      SDL_GPUBuffer *point_light_ssbo,
                      SDL_GPUBuffer *light_grid_ssbo,
                      SDL_GPUBuffer *global_index_ssbo);

    bool is_skinned_ready() const { return skinned_initialized; }

private:
    void emit_cylinder(const glm::vec3 &a, const glm::vec3 &b,
                       float radius, glm::vec3 color, int sides,
                       std::vector<BasaltVertex> &out_verts);

    // Bone-shaped octahedron: point at tail (a), widest at 20% from a,
    // tapering to a point at tip (b). Classic rig bone visualisation.
    void emit_bone_oct(const glm::vec3 &a, const glm::vec3 &b,
                       float width, glm::vec3 color,
                       std::vector<BasaltVertex> &out_verts);

    // UV sphere for joint pivot markers (sectors longitude, rings latitude).
    void emit_sphere(const glm::vec3 &center, float radius, glm::vec3 color,
                     int sectors, int rings,
                     std::vector<BasaltVertex> &out_verts);

    // RGB world-axis tripod at center: X=red, Y=green, Z=blue.
    void emit_tripod(const glm::vec3 &center, float size,
                     std::vector<BasaltVertex> &out_verts);

    void emit_box(const glm::vec3 &center, float half_size, glm::vec3 color,
                  std::vector<BasaltVertex> &out_verts);

    // Wireframe box: 12 edges as thin cylinders. Distinct from solid emit_box.
    void emit_wireframe_box(const glm::vec3 &center, float half_size,
                            float edge_radius, glm::vec3 color,
                            std::vector<BasaltVertex> &out_verts);

    void emit_diamond(const glm::vec3 &center, float radius, glm::vec3 color,
                      std::vector<BasaltVertex> &out_verts);

    // Flat filled disc on the XY plane at center.z (normal = +Z).
    // Used as the World Root ground marker.
    void emit_flat_circle(const glm::vec3 &center, float radius, glm::vec3 color,
                          int segments, std::vector<BasaltVertex> &out_verts);

    static constexpr uint32_t MAX_RIG_VERTICES = 65536; // ~2.5 MB

    bool                     initialized   = false;
    SDL_GPUGraphicsPipeline *pipeline      = nullptr; // borrowed
    SDL_GPUBuffer           *dummy_ssbo_   = nullptr; // borrowed
    SDL_GPUDevice           *gpu_device    = nullptr;
    AssetManager            *asset_manager = nullptr;

    SDL_GPUBuffer         *rig_vbo       = nullptr; // owned, static-sized
    SDL_GPUTransferBuffer *transfer_buf  = nullptr; // owned, persistent mapped staging

    // Skinned mesh GPU resources
    bool                     skinned_initialized = false;
    SDL_GPUGraphicsPipeline *skinned_pipeline    = nullptr; // owned
    SDL_GPUBuffer           *skinned_vbo         = nullptr; // owned, static
    SDL_GPUBuffer           *skinned_ibo         = nullptr; // owned, static
    SDL_GPUBuffer           *joint_ssbo          = nullptr; // owned, per-frame palette
    SDL_GPUTransferBuffer   *joint_transfer      = nullptr; // owned, upload staging
    uint32_t                 skinned_index_count  = 0;
    uint32_t                 skinned_joint_count  = 0;

    SkeletonDef              skeleton_def;
    JointMapping             joint_mapping;
    std::vector<glm::mat4>   joint_palette_cache;

    bool init_skinned_pipeline(SDL_GPUDevice *device, SDL_Window *window,
                               SDL_GPUTextureFormat depth_fmt, AssetManager *am);
};
