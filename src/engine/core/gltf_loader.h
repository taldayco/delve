#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct GltfVertex {
    glm::vec3 position;    // 12B (offset 0)
    glm::vec3 normal;      // 12B (offset 12)
    glm::vec2 texcoord;    //  8B (offset 24)
    glm::vec4 tangent;     // 16B (offset 32, xyz=tangent, w=handedness)
};  // 48 bytes total
static_assert(sizeof(GltfVertex) == 48, "GltfVertex must be 48 bytes");

struct GltfMeshData {
    std::string             name;
    std::vector<GltfVertex> vertices;
    std::vector<uint32_t>   indices;
    int                     material_index = -1;
};

struct GltfTextureData {
    std::string          name;
    std::vector<uint8_t> pixels;  // RGBA8
    int width = 0, height = 0;
    bool srgb = true;  // true for base_color, false for normal/ORM
};

struct GltfAsset {
    std::vector<GltfMeshData>    meshes;
    std::vector<GltfTextureData> textures;
    bool        ok = false;
    std::string error;
};

GltfAsset load_gltf(const std::string &path);
