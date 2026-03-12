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

    // Skinning data (populated when JOINTS_0/WEIGHTS_0 attributes exist)
    std::vector<glm::u8vec4> joint_indices;  // per-vertex, 4 joints [0..255]
    std::vector<glm::vec4>   weights;        // per-vertex, 4 weights (sum to 1.0)
    int                      skin_index = -1; // index into GltfAsset::skins

    bool has_skinning() const {
        return !joint_indices.empty() && !weights.empty() && skin_index >= 0;
    }
};

struct GltfTextureData {
    std::string          name;
    std::vector<uint8_t> pixels;  // RGBA8
    int width = 0, height = 0;
    bool srgb = true;  // true for base_color, false for normal/ORM
};

struct GltfSkinJoint {
    int32_t     parent_index = -1;  // -1 for root
    std::string name;
    glm::vec3   translation{0.0f};
    glm::vec4   rotation{0.0f, 0.0f, 0.0f, 1.0f};  // xyzw quaternion
    glm::vec3   scale{1.0f};
};

struct GltfSkinData {
    std::vector<GltfSkinJoint> joints;
    std::vector<glm::mat4>     inverse_bind_matrices;
    std::vector<int32_t>       gltf_node_indices;  // original node indices
};

struct GltfAsset {
    std::vector<GltfMeshData>    meshes;
    std::vector<GltfTextureData> textures;
    std::vector<GltfSkinData>    skins;
    bool        ok = false;
    std::string error;

    bool has_skinned_meshes() const { return !skins.empty(); }
};

GltfAsset load_gltf(const std::string &path);
