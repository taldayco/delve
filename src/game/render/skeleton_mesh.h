#pragma once
#include "actor.h"
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <vector>

// Skeleton is an alias for SkeletonPose (array of joint world positions).
using Skeleton = SkeletonPose;

// Per-vertex data for a CPU-skinned (LBS) skeletal mesh.
// bone_index0/1: primary/secondary bone indices [0..NUM_BONES-1] stored as float
//               so they feed directly into float vertex attributes without reinterpret.
// bone_weight:  blend weight for bone_index0; (1 - bone_weight) for bone_index1.
struct SkeletonVertex {
    glm::vec3 position;    // world-space (updated by deform_skeleton_mesh each frame)
    glm::vec3 normal;      // world-space (updated by deform_skeleton_mesh each frame)
    float     bone_index0; // primary bone index [0..17]
    float     bone_weight; // blend weight for bone_index0
    float     bone_index1; // secondary bone index [0..17]
};
static_assert(sizeof(SkeletonVertex) == 36, "SkeletonVertex must be 36 bytes");

// Alternative per-vertex layout using integer bone indices (for GPU skinning path).
// bone_index: primary (.x) and secondary (.y) bone indices [0..NUM_BONES-1].
// bone_weight: blend weight for bone_index.x; (1 - bone_weight) for bone_index.y.
struct BoneVertex {
    glm::vec3 position;    // local rest-pose position (deformed to world each frame)
    glm::vec3 normal;      // local rest-pose normal
    glm::ivec2 bone_index; // primary (.x) and secondary (.y) bone indices
    float      bone_weight;// blend weight for primary bone; secondary = 1 - bone_weight
};

// Per-bone-segment profile for procedural mesh generation.
// One entry per bone in the BONES table (16 total, matching BoneSeg order).
struct BoneProfile {
    float      radius_start = 0.06f; // cross-section radius at the start (parent) joint
    float      radius_end   = 0.06f; // cross-section radius at the end (child) joint
    int        sides        = 6;     // polygon cross-section side count [3..6]
    float      taper        = 0.0f;  // unused by mesh gen (use radius_end directly); kept for compat
    float      twist        = 0.0f;  // cross-section rotation along bone axis in degrees
    glm::vec3  color        = glm::vec3(0.5f, 0.5f, 0.5f); // per-bone display color
};

// 16-element array — one entry per bone (matches BoneSeg order in actor_renderer.h).
static constexpr int NUM_BONE_PROFILES = 16;
using BoneProfileArray = std::array<BoneProfile, NUM_BONE_PROFILES>;

// GPU-ready CPU-skinned mesh.
// Vertex/index data lives in CPU vectors and is re-uploaded every frame after deform.
struct SkeletonMesh {
    std::vector<SkeletonVertex> vertices;
    std::vector<uint32_t>       indices;

    // Bone-local rest-pose positions and normals for each vertex (same length as vertices).
    // Used by deform_skeleton_mesh to reconstruct world positions/normals from current pose.
    std::vector<glm::vec3> rest_positions;
    std::vector<glm::vec3> rest_normals;

    // Per-vertex primary bone index (same length as vertices).
    std::vector<int>   vertex_bone;
    // Per-vertex blend weight for the primary bone; secondary weight = 1 - w.
    std::vector<float> vertex_bone_weight;

    // Owned GPU buffers (null until explicitly uploaded).
    SDL_GPUBuffer *vertex_buffer = nullptr;
    SDL_GPUBuffer *index_buffer  = nullptr;

    bool is_uploaded() const { return vertex_buffer != nullptr; }

    void release(SDL_GPUDevice *device) {
        if (vertex_buffer) { SDL_ReleaseGPUBuffer(device, vertex_buffer); vertex_buffer = nullptr; }
        if (index_buffer)  { SDL_ReleaseGPUBuffer(device, index_buffer);  index_buffer  = nullptr; }
    }
};

// Build a CPU-only SkeletonMesh from the given bind pose + per-bone profiles.
// profiles[bi] describes the cross-section for bone bi (0..NUM_BONE_PROFILES-1).
// Positions are stored in local bone space (rest_positions). vertices[i].position
// is set to the world position at bind pose. Call deform_skeleton_mesh each frame.
SkeletonMesh generate_skeleton_mesh(const SkeletonPose     &bind_pose,
                                    const BoneProfileArray &profiles);

// Overload accepting Skeleton (= SkeletonPose) and a vector of BoneProfile.
SkeletonMesh generate_skeleton_mesh(const Skeleton                  &skel,
                                    const std::vector<BoneProfile>  &profiles);

// LBS deform: for each vertex, reconstruct world position/normal from local bone-space
// rest_positions using the current joint positions in `pose`.
// bone_weight blends between bone_index0 and bone_index1 transforms.
void deform_skeleton_mesh(SkeletonMesh &mesh, const SkeletonPose &pose);


