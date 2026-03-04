#pragma once
#include "actor.h"
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <vector>

// Per-vertex data for skinned skeletal mesh rendering.
// bone_indices: two influencing joint indices (ivec2)
// bone_weight:  blend weight for bone_indices[0]; (1 - bone_weight) for [1]
struct SkeletonVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::ivec2 bone_indices;  // joint indices into SkeletonPose::joints
    float      bone_weight;   // weight for bone_indices[0]
};

// Cylindrical cross-section profile for one bone segment.
// Drives procedural mesh generation around the bone axis.
struct BoneProfile {
    float radius  = 0.06f;   // cylinder radius in world units
    float taper   = 0.0f;    // 0 = uniform, 1 = fully tapered at distal end
    int   sides   = 6;       // polygon cross-section resolution
};

// 18-element array — one entry per joint (Joint::COUNT = 17) plus one spare.
using BoneProfiles = std::array<BoneProfile, 18>;

// GPU-ready skinned mesh.
// Vertex/index data lives in CPU vectors until upload_to_gpu() is called.
struct SkeletonMesh {
    std::vector<SkeletonVertex> vertices;
    std::vector<uint32_t>       indices;

    // Local bone-space positions for each vertex (same length as vertices).
    // Used by deform_skeleton_mesh to recompute world positions each call.
    std::vector<glm::vec3> rest_positions;

    // Owned GPU buffers (null until explicitly uploaded).
    SDL_GPUBuffer *vertex_buffer = nullptr;
    SDL_GPUBuffer *index_buffer  = nullptr;

    bool is_uploaded() const { return vertex_buffer != nullptr; }

    void release(SDL_GPUDevice *device) {
        if (vertex_buffer) { SDL_ReleaseGPUBuffer(device, vertex_buffer); vertex_buffer = nullptr; }
        if (index_buffer)  { SDL_ReleaseGPUBuffer(device, index_buffer);  index_buffer  = nullptr; }
    }
};

struct SkeletonPose;

// Build a CPU-only SkeletonMesh from the given bind pose + per-bone profiles.
// Positions are stored in local bone space (rest_positions). vertices[i].position
// is set to the world position at bind pose. Call deform_skeleton_mesh each
// frame to update positions to a new pose.
SkeletonMesh generate_skeleton_mesh(const SkeletonPose &bind_pose,
                                    const BoneProfiles &profiles);

// LBS deform: for each vertex, reconstruct world position from local bone-space
// rest_positions using the current joint positions in `pose`.
// bone_weight blends between bone_indices[0] and bone_indices[1] transforms.
void deform_skeleton_mesh(SkeletonMesh &mesh, const SkeletonPose &pose);
