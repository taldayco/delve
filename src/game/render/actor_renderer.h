#pragma once
#include "terrain/terrain_mesh.h"   // BasaltVertex, SceneUniforms
#include "gpu/gpu.h"                // UploadManager
#include "core/asset_manager.h"
#include <SDL3/SDL_gpu.h>
#include <flecs.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Classical 8-head proportion system. All fields in world-space feet.
struct ActorProportions {
    float total_height;
    float head_height;
    float neck_height;
    float torso_height;
    float upper_leg;
    float lower_leg;
    float shoulder_width;
    float hip_width;
    float waist_width;
    float head_width;
    float upper_arm;
    float forearm;
    float hand_length;
    float foot_length;
};

// A single skeletal joint with world-space position and ellipsoid radii.
struct SkeletonJoint {
    glm::vec3   position; // local actor space, origin at feet
    glm::vec3   size;     // ellipsoid radii (x=width, y=depth, z=height)
    std::string name;
};

class ActorRenderer {
public:
    // Factory: derive all proportions from total height using 8-head system.
    static ActorProportions make_proportions(float total_height_feet);

    // Build skeleton joints in local actor space (origin at feet).
    static std::vector<SkeletonJoint> build_skeleton(const ActorProportions &p);

    // terrain_pipeline and dummy_ssbo are borrowed (not owned) from TerrainRenderer.
    // dummy_ssbo fills binding slots 1 and 2 (pipeline declares num_storage_buffers=3).
    void init(SDL_GPUDevice *device,
              SDL_GPUGraphicsPipeline *terrain_pipeline,
              SDL_GPUBuffer *dummy_ssbo,
              AssetManager *am);

    // Call BEFORE the actor render pass is opened.
    // Builds geometry from ECS, uploads to actor_vbo via a copy pass.
    // Returns the number of vertices to draw (0 if nothing to draw).
    uint32_t prepare(SDL_GPUCommandBuffer *cmd, flecs::world &ecs);

    // Call inside the actor render pass.
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

    static constexpr uint32_t MAX_ACTOR_VERTICES = 32768; // ~1.2 MB

    bool                     initialized   = false;
    SDL_GPUGraphicsPipeline *pipeline      = nullptr; // borrowed
    SDL_GPUBuffer           *dummy_ssbo_   = nullptr; // borrowed
    SDL_GPUDevice           *gpu_device    = nullptr;
    AssetManager            *asset_manager = nullptr;

    SDL_GPUBuffer         *actor_vbo      = nullptr; // owned, static-sized
    SDL_GPUTransferBuffer *transfer_buf   = nullptr; // owned, persistent mapped staging
};
