#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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

// ---- Skinned mesh types ----

struct SkinnedVertex {
    glm::vec3    position;   // 12B
    glm::vec3    normal;     // 12B
    glm::vec2    texcoord;   //  8B
    glm::vec4    tangent;    // 16B
    uint8_t      joints[4];  //  4B
    float        weights[4]; // 16B
};  // 68 bytes total
static_assert(sizeof(SkinnedVertex) == 68, "SkinnedVertex must be 68 bytes");

struct GltfBone {
    std::string  name;
    int          parent_index;           // -1 for root
    glm::mat4    inverse_bind_matrix;
    glm::mat4    local_rest_transform;
};

struct GltfSkeleton {
    std::vector<GltfBone> bones;
    int                   root_bone_index = 0;
};

struct GltfAnimKeyframe {
    float      time;
    glm::vec3  translation;
    glm::quat  rotation;
    glm::vec3  scale;
};

struct GltfAnimChannel {
    int                          bone_index;
    std::string                  path;   // "translation" | "rotation" | "scale"
    std::vector<float>           times;
    std::vector<glm::vec3>       translations;
    std::vector<glm::quat>       rotations;
    std::vector<glm::vec3>       scales;
};

struct GltfAnimationClip {
    std::string                  name;
    float                        duration;
    std::vector<GltfAnimChannel> channels;
};

struct GltfSkinnedMeshData {
    std::string                  name;
    std::vector<SkinnedVertex>   vertices;
    std::vector<uint32_t>        indices;
    int                          material_index = -1;
};

struct GltfSkinnedAsset {
    std::vector<GltfSkinnedMeshData> meshes;
    std::vector<GltfTextureData>     textures;
    GltfSkeleton                     skeleton;
    std::vector<GltfAnimationClip>   animations;
    // Flat inverse bind matrix array (one per bone, same order as skeleton.bones)
    std::vector<glm::mat4>           inverse_bind_matrices;
    // Root scene node transform (captures Blender Y→Z-up +90° X rotation, etc.)
    glm::mat4                        root_transform{1.0f};
    bool        ok = false;
    std::string error;
};

GltfSkinnedAsset load_gltf_skinned(const std::string &path);
