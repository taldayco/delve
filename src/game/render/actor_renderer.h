#pragma once
#include "terrain/terrain_mesh.h"   // BasaltVertex, SceneUniforms
#include "gpu/gpu.h"                // UploadManager
#include "core/asset_manager.h"
#include "render/skeleton_mesh.h"   // SkeletonVertex, SkeletonMesh, BoneProfile, BoneProfileArray
#include <SDL3/SDL_gpu.h>
#include <flecs.h>
#include <glm/glm.hpp>
#include <vector>

// Indices into SegmentProfiles::bones[].
// Order must match the BONES table in skeleton_mesh.cpp.
enum class BoneSeg : int {
    SPINE = 0,        // ROOT → SPINE
    CHEST_CORE,       // SPINE → CHEST
    NECK_SEG,         // CHEST → NECK
    HEAD_SEG,         // NECK → HEAD
    L_SHOULDER_CONN,  // CHEST → L_SHOULDER
    L_UPPER_ARM,      // L_SHOULDER → L_ELBOW
    L_FOREARM,        // L_ELBOW → L_WRIST
    R_SHOULDER_CONN,  // CHEST → R_SHOULDER
    R_UPPER_ARM,      // R_SHOULDER → R_ELBOW
    R_FOREARM,        // R_ELBOW → R_WRIST
    L_HIP_CONN,       // SPINE → L_HIP
    L_UPPER_LEG,      // L_HIP → L_KNEE
    L_LOWER_LEG,      // L_KNEE → L_ANKLE
    R_HIP_CONN,       // SPINE → R_HIP
    R_UPPER_LEG,      // R_HIP → R_KNEE
    R_LOWER_LEG,      // R_KNEE → R_ANKLE
    COUNT
};
static_assert((int)BoneSeg::COUNT == NUM_BONE_PROFILES,
              "BoneSeg::COUNT must equal NUM_BONE_PROFILES");

// Per-segment profile array used by the ImGui UI and generate_skeleton_mesh.
// Indexed by BoneSeg. Array layout matches BONES table in skeleton_mesh.cpp.
struct SegmentProfiles {
    BoneProfile bones[(int)BoneSeg::COUNT];

    SegmentProfiles() {
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

    // Convert to BoneProfileArray for generate_skeleton_mesh.
    BoneProfileArray to_profile_array() const {
        BoneProfileArray arr;
        for (int i = 0; i < (int)BoneSeg::COUNT; ++i) arr[(size_t)i] = bones[i];
        return arr;
    }
};

struct SkeletonPose;

class ActorRenderer {
public:
    // terrain_pipeline and dummy_ssbo are borrowed (not owned) from TerrainRenderer.
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

    // Upload SkeletonMesh vertex/index data to GPU buffers.
    // Call this each frame after deform_skeleton_mesh.
    void upload_mesh(SDL_GPUCommandBuffer *cmd, const SkeletonMesh &mesh);

    // Call inside the actor render pass.
    void draw(SDL_GPURenderPass *pass,
              SDL_GPUCommandBuffer *cmd,
              const SceneUniforms &uniforms,
              SDL_GPUBuffer *point_light_ssbo,
              uint32_t vertex_count);

    // Draw the uploaded skeleton mesh using the skel pipeline.
    // Vertices are already in world space (CPU LBS done before upload).
    void draw_skel(SDL_GPURenderPass *pass,
                   SDL_GPUCommandBuffer *cmd,
                   const SceneUniforms &uniforms);

    // Rebuild actor vertex buffer from pose + per-bone profiles (old BasaltVertex path).
    void regenerate(const SkeletonPose &pose, SegmentProfiles &profiles);

    // Draw ImGui panel with per-bone sliders. Sets skel_profiles_dirty_ on any change.
    void draw_ui(const SkeletonPose &pose);

    // Returns true (and resets the flag) if draw_ui changed profiles since last call.
    bool consume_skel_profiles_dirty() {
        bool v = skel_profiles_dirty_;
        skel_profiles_dirty_ = false;
        return v;
    }

    void cleanup(SDL_GPUDevice *device);

    bool is_initialized() const { return initialized; }
    bool has_skel_pipeline() const { return skel_pipeline != nullptr; }

    SegmentProfiles segment_profiles;

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

    // Cylinder-based actor buffers (existing BasaltVertex path).
    SDL_GPUBuffer         *actor_vbo      = nullptr;
    SDL_GPUTransferBuffer *transfer_buf   = nullptr;

    // Skeleton mesh pipeline + buffers (owned).
    SDL_GPUGraphicsPipeline *skel_pipeline    = nullptr;
    SDL_GPUBuffer           *skel_vbo         = nullptr;
    SDL_GPUBuffer           *skel_ibo         = nullptr;
    SDL_GPUTransferBuffer   *skel_transfer    = nullptr;
    uint32_t                 skel_index_count = 0;
    glm::vec4                skel_bone_colors[(int)BoneSeg::COUNT] = {};

    // Hot-reload state for BasaltVertex path.
    std::vector<BasaltVertex> regen_verts_;
    bool                      regen_dirty_        = false;
    uint32_t                  regen_vertex_count_ = 0;

    // Set by draw_ui when profile sliders change; consumed by topo_game to re-generate mesh.
    bool skel_profiles_dirty_ = false;

    // Cached skeleton mesh — updated by generate_skeleton_mesh when profiles change.
    // upload_mesh() uploads this to skel_vbo/skel_ibo each frame after deform.
    SkeletonMesh cached_mesh;
};
