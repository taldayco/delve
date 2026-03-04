#pragma once
#include "terrain/terrain_mesh.h"   // BasaltVertex, SceneUniforms
#include "gpu/gpu.h"                // UploadManager
#include "core/asset_manager.h"
#include <SDL3/SDL_gpu.h>
#include <flecs.h>
#include <glm/glm.hpp>
#include <vector>

// Vertex layout for GPU-skinned skeleton meshes.
struct SkeletonVertex {
    float pos_x, pos_y, pos_z;
    float nx, ny, nz;
    float bone_index;   // primary bone (0–17), stored as float
    float bone_weight;  // blend weight [0,1] for primary bone
    float bone_index2;  // secondary bone
};

// CPU-side skeleton mesh with bone matrices and per-bone colors.
struct SkeletonMesh {
    std::vector<SkeletonVertex> vertices;
    std::vector<uint32_t>       indices;
    glm::mat4 bone_matrices[18];
    glm::vec3 bone_colors[18];
};

// Per-bone-segment rendering profile for hot-reload UI.
struct BoneProfile {
    float radius_start = 0.06f;
    float radius_end   = 0.06f; // taper: end radius (same = no taper)
    int   sides        = 6;     // polygon count [3, 6]
    float twist        = 0.0f;  // rotation along bone axis in degrees
};

// Indices into BoneProfiles::bones[].
enum class BoneSeg : int {
    SPINE = 0,      // ROOT → SPINE
    CHEST_CORE,     // SPINE → CHEST
    NECK_SEG,       // CHEST → NECK
    HEAD_SEG,       // NECK → HEAD
    L_SHOULDER_CONN,// CHEST → L_SHOULDER
    L_UPPER_ARM,    // L_SHOULDER → L_ELBOW
    L_FOREARM,      // L_ELBOW → L_WRIST
    R_SHOULDER_CONN,// CHEST → R_SHOULDER
    R_UPPER_ARM,    // R_SHOULDER → R_ELBOW
    R_FOREARM,      // R_ELBOW → R_WRIST
    L_HIP_CONN,     // ROOT → L_HIP
    L_UPPER_LEG,    // L_HIP → L_KNEE
    L_LOWER_LEG,    // L_KNEE → L_ANKLE
    R_HIP_CONN,     // ROOT → R_HIP
    R_UPPER_LEG,    // R_HIP → R_KNEE
    R_LOWER_LEG,    // R_KNEE → R_ANKLE
    COUNT
};

// Per-segment overrides. Initialised to match ActorConfig defaults.
struct BoneProfiles {
    BoneProfile bones[(int)BoneSeg::COUNT];

    BoneProfiles() {
        // Torso segments: 4-sided, thicker
        for (int i : { (int)BoneSeg::SPINE, (int)BoneSeg::CHEST_CORE,
                       (int)BoneSeg::NECK_SEG, (int)BoneSeg::HEAD_SEG }) {
            bones[i].radius_start = 0.12f;
            bones[i].radius_end   = 0.12f;
            bones[i].sides        = 4;
        }
        // Limb segments: 6-sided, thinner
        for (int i = (int)BoneSeg::L_SHOULDER_CONN; i < (int)BoneSeg::COUNT; ++i) {
            bones[i].radius_start = 0.06f;
            bones[i].radius_end   = 0.06f;
            bones[i].sides        = 6;
        }
    }
};

struct SkeletonPose;

class ActorRenderer {
public:
    // terrain_pipeline and dummy_ssbo are borrowed (not owned) from TerrainRenderer.
    // dummy_ssbo fills binding slots 1 and 2 (pipeline declares num_storage_buffers=3).
    void init(SDL_GPUDevice *device,
              SDL_GPUGraphicsPipeline *terrain_pipeline,
              SDL_GPUBuffer *dummy_ssbo,
              AssetManager *am);

    // Call after init() to build the skeleton GPU pipeline.
    // depth_fmt must match the depth texture used in the render pass.
    void init_skel_pipeline(SDL_GPUDevice *device,
                            SDL_Window *window,
                            SDL_GPUTextureFormat depth_fmt);

    // Call BEFORE the actor render pass is opened.
    // Builds geometry from ECS, uploads to actor_vbo via a copy pass.
    // Returns the number of vertices to draw (0 if nothing to draw).
    uint32_t prepare(SDL_GPUCommandBuffer *cmd, flecs::world &ecs);

    // Upload SkeletonMesh vertex/index data to GPU buffers and cache bone data.
    void upload_mesh(SDL_GPUCommandBuffer *cmd, const SkeletonMesh &mesh);

    // Call inside the actor render pass.
    void draw(SDL_GPURenderPass *pass,
              SDL_GPUCommandBuffer *cmd,
              const SceneUniforms &uniforms,
              SDL_GPUBuffer *point_light_ssbo,
              uint32_t vertex_count);

    // Draw the uploaded skeleton mesh using the skel pipeline.
    void draw_skel(SDL_GPURenderPass *pass,
                   SDL_GPUCommandBuffer *cmd,
                   const SceneUniforms &uniforms);

    // Rebuild actor vertex buffer from pose + per-bone profiles.
    // Flags the next prepare() to upload the rebuilt verts.
    // Index buffer is not touched; vertex count must stay <= MAX_ACTOR_VERTICES.
    void regenerate(const SkeletonPose &pose, BoneProfiles &profiles);

    // Draw ImGui panel with per-bone sliders. Calls regenerate() on any change.
    void draw_ui(const SkeletonPose &pose);

    void cleanup(SDL_GPUDevice *device);

    bool is_initialized() const { return initialized; }

    BoneProfiles bone_profiles;

private:
    void emit_cylinder(const glm::vec3 &a, const glm::vec3 &b,
                       float radius, glm::vec3 color, int sides,
                       std::vector<BasaltVertex> &out_verts);

    void emit_cylinder_ex(const glm::vec3 &a, const glm::vec3 &b,
                          float r_start, float r_end,
                          glm::vec3 color, int sides, float twist_rad,
                          std::vector<BasaltVertex> &out_verts);

    static constexpr uint32_t MAX_ACTOR_VERTICES = 32768;
    static constexpr uint32_t MAX_SKEL_VERTICES  = 16384;
    static constexpr uint32_t MAX_SKEL_INDICES   = 65536;

    bool                     initialized   = false;
    SDL_GPUGraphicsPipeline *pipeline      = nullptr; // borrowed (terrain)
    SDL_GPUBuffer           *dummy_ssbo_   = nullptr; // borrowed
    SDL_GPUDevice           *gpu_device    = nullptr;
    AssetManager            *asset_manager = nullptr;

    // Cylinder-based actor buffers (existing).
    SDL_GPUBuffer         *actor_vbo      = nullptr;
    SDL_GPUTransferBuffer *transfer_buf   = nullptr;

    // Skeleton mesh pipeline + buffers (owned).
    SDL_GPUGraphicsPipeline *skel_pipeline    = nullptr;
    SDL_GPUBuffer           *skel_vbo         = nullptr;
    SDL_GPUBuffer           *skel_ibo         = nullptr;
    SDL_GPUTransferBuffer   *skel_transfer    = nullptr;
    uint32_t                 skel_index_count = 0;
    glm::mat4                skel_bone_matrices[18] = {};
    glm::vec4                skel_bone_colors[18]   = {};

    // Hot-reload state: rebuilt verts waiting to be uploaded in next prepare().
    std::vector<BasaltVertex> regen_verts_;
    bool                      regen_dirty_        = false;
    uint32_t                  regen_vertex_count_ = 0;
};
