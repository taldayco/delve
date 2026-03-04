#pragma once
#include "terrain/terrain_mesh.h"   // BasaltVertex, SceneUniforms
#include "gpu/gpu.h"                // UploadManager
#include "core/asset_manager.h"
#include "render/skeleton_mesh.h"   // SkeletonVertex, SkeletonMesh, BoneProfile, BoneProfileArray
#include "config.h"
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
    bool dirty = false; // set to true when any parameter changes; cleared after mesh rebuild

    SegmentProfiles() {
        // Anatomical radii derived from Config constants.
        static constexpr float R_HEAD      = Config::ACTOR_HEAD_WIDTH_WU  / 2.0f;
        static constexpr float R_SHOULDER  = Config::ACTOR_SHOULDER_WIDTH_WU / 2.0f;
        static constexpr float R_HIP       = Config::ACTOR_HIP_WIDTH_WU   / 2.0f;
        static constexpr float R_WAIST     = Config::ACTOR_WAIST_WIDTH_WU / 2.0f;
        static constexpr float R_UPPER_ARM = Config::ACTOR_UPPER_ARM_WU   * 0.18f;
        static constexpr float R_FOREARM   = Config::ACTOR_FOREARM_WU     * 0.15f;
        static constexpr float R_UPPER_LEG = Config::ACTOR_UPPER_LEG_WU   * 0.22f;
        static constexpr float R_LOWER_LEG = Config::ACTOR_LOWER_LEG_WU   * 0.16f;
        static constexpr float R_NECK      = R_HEAD * 0.55f;

        // SPINE: ROOT→SPINE (hip region, tapers to waist)
        bones[(int)BoneSeg::SPINE].radius_start = R_HIP;
        bones[(int)BoneSeg::SPINE].radius_end   = R_WAIST;
        bones[(int)BoneSeg::SPINE].sides        = 6;
        bones[(int)BoneSeg::SPINE].color        = glm::vec3(0.25f, 0.25f, 0.30f);

        // CHEST_CORE: SPINE→CHEST (waist to shoulder width)
        bones[(int)BoneSeg::CHEST_CORE].radius_start = R_WAIST;
        bones[(int)BoneSeg::CHEST_CORE].radius_end   = R_SHOULDER;
        bones[(int)BoneSeg::CHEST_CORE].sides        = 6;
        bones[(int)BoneSeg::CHEST_CORE].color        = glm::vec3(0.25f, 0.25f, 0.30f);

        // Neck
        bones[(int)BoneSeg::NECK_SEG].radius_start = R_NECK;
        bones[(int)BoneSeg::NECK_SEG].radius_end   = R_NECK;
        bones[(int)BoneSeg::NECK_SEG].sides        = 6;
        bones[(int)BoneSeg::NECK_SEG].color        = glm::vec3(0.35f, 0.30f, 0.28f);

        // Head
        bones[(int)BoneSeg::HEAD_SEG].radius_start = R_HEAD;
        bones[(int)BoneSeg::HEAD_SEG].radius_end   = R_HEAD;
        bones[(int)BoneSeg::HEAD_SEG].sides        = 8;
        bones[(int)BoneSeg::HEAD_SEG].color        = glm::vec3(0.35f, 0.30f, 0.28f);

        // Shoulder connectors (CHEST→L/R_SHOULDER): taper from shoulder to upper-arm radius
        for (int i : { (int)BoneSeg::L_SHOULDER_CONN, (int)BoneSeg::R_SHOULDER_CONN }) {
            bones[i].radius_start = R_SHOULDER * 0.4f;
            bones[i].radius_end   = R_UPPER_ARM;
            bones[i].sides        = 5;
            bones[i].color        = glm::vec3(0.20f, 0.22f, 0.28f);
        }

        // Upper arms
        for (int i : { (int)BoneSeg::L_UPPER_ARM, (int)BoneSeg::R_UPPER_ARM }) {
            bones[i].radius_start = R_UPPER_ARM;
            bones[i].radius_end   = R_UPPER_ARM * 0.85f;
            bones[i].sides        = 5;
            bones[i].color        = glm::vec3(0.20f, 0.22f, 0.28f);
        }

        // Forearms
        for (int i : { (int)BoneSeg::L_FOREARM, (int)BoneSeg::R_FOREARM }) {
            bones[i].radius_start = R_FOREARM;
            bones[i].radius_end   = R_FOREARM * 0.75f;
            bones[i].sides        = 4;
            bones[i].color        = glm::vec3(0.22f, 0.24f, 0.30f);
        }

        // Hip connectors (SPINE→L/R_HIP): taper from hip to upper-leg radius
        for (int i : { (int)BoneSeg::L_HIP_CONN, (int)BoneSeg::R_HIP_CONN }) {
            bones[i].radius_start = R_HIP * 0.5f;
            bones[i].radius_end   = R_UPPER_LEG;
            bones[i].sides        = 6;
            bones[i].color        = glm::vec3(0.18f, 0.20f, 0.26f);
        }

        // Upper legs
        for (int i : { (int)BoneSeg::L_UPPER_LEG, (int)BoneSeg::R_UPPER_LEG }) {
            bones[i].radius_start = R_UPPER_LEG;
            bones[i].radius_end   = R_UPPER_LEG * 0.80f;
            bones[i].sides        = 6;
            bones[i].color        = glm::vec3(0.18f, 0.20f, 0.26f);
        }

        // Lower legs
        for (int i : { (int)BoneSeg::L_LOWER_LEG, (int)BoneSeg::R_LOWER_LEG }) {
            bones[i].radius_start = R_LOWER_LEG;
            bones[i].radius_end   = R_LOWER_LEG * 0.70f;
            bones[i].sides        = 5;
            bones[i].color        = glm::vec3(0.16f, 0.18f, 0.24f);
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
