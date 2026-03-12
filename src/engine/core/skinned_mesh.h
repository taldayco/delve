#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

struct SkinnedVertex {
    float pos_x, pos_y, pos_z;        // 12B  (location 0: FLOAT3)
    float nx, ny, nz;                 // 12B  (location 1: FLOAT3)
    float u, v;                       //  8B  (location 2: FLOAT2)
    float color_r, color_g, color_b;  // 12B  (location 3: FLOAT3)
    float roughness;                  //  4B  (location 4: FLOAT)
    float metallic;                   //  4B  (location 5: FLOAT)
    uint32_t joint_indices;           //  4B  (location 6: UBYTE4, packed)
    float weight_x, weight_y, weight_z, weight_w; // 16B (location 7: FLOAT4)
};  // 72 bytes total
static_assert(sizeof(SkinnedVertex) == 72, "SkinnedVertex must be 72 bytes");

inline uint32_t pack_joint_indices(uint8_t j0, uint8_t j1, uint8_t j2, uint8_t j3) {
    return (uint32_t)j0 | ((uint32_t)j1 << 8) | ((uint32_t)j2 << 16) | ((uint32_t)j3 << 24);
}

struct SkinnedMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t>      indices;
    std::vector<glm::mat4>     inverse_bind_matrices;
    uint32_t                   joint_count = 0;
    glm::vec3 aabb_min{0}, aabb_max{0};
};

struct GltfMeshData;
struct GltfSkinData;

SkinnedMeshData build_skinned_mesh(const GltfMeshData &mesh, const GltfSkinData &skin);
