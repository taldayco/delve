#include "core/gltf_loader.h"
#include <cgltf.h>
#include <stb_image.h>
#include <SDL3/SDL_log.h>
#include <filesystem>

namespace fs = std::filesystem;

// --- Image extraction helpers ---

static GltfTextureData load_image_from_buffer(const cgltf_buffer_view *view,
                                               const char *name, bool srgb) {
    GltfTextureData tex;
    tex.name = name ? name : "";
    tex.srgb = srgb;

    const uint8_t *data = (const uint8_t *)view->buffer->data + view->offset;
    int w, h, ch;
    uint8_t *pixels = stbi_load_from_memory(data, (int)view->size, &w, &h, &ch, 4);
    if (!pixels) return tex;

    tex.width  = w;
    tex.height = h;
    tex.pixels.assign(pixels, pixels + w * h * 4);
    stbi_image_free(pixels);
    return tex;
}

static GltfTextureData load_image_from_file(const std::string &uri,
                                              const std::string &base_dir,
                                              const char *name, bool srgb) {
    GltfTextureData tex;
    tex.name = name ? name : "";
    tex.srgb = srgb;

    std::string full_path = base_dir + "/" + uri;
    int w, h, ch;
    uint8_t *pixels = stbi_load(full_path.c_str(), &w, &h, &ch, 4);
    if (!pixels) return tex;

    tex.width  = w;
    tex.height = h;
    tex.pixels.assign(pixels, pixels + w * h * 4);
    stbi_image_free(pixels);
    return tex;
}

static bool is_srgb_texture(const cgltf_material *mat, const cgltf_texture *tex) {
    if (!mat || !tex) return true;
    // Base color textures are sRGB; normal and metallic-roughness are linear
    if (mat->has_pbr_metallic_roughness) {
        if (mat->pbr_metallic_roughness.base_color_texture.texture == tex)
            return true;
        if (mat->pbr_metallic_roughness.metallic_roughness_texture.texture == tex)
            return false;
    }
    if (mat->normal_texture.texture == tex) return false;
    if (mat->occlusion_texture.texture == tex) return false;
    if (mat->emissive_texture.texture == tex) return true;
    return true;
}

// --- Mesh extraction ---

static void extract_mesh(const cgltf_mesh *mesh, const cgltf_data *data,
                          std::vector<GltfMeshData> &out) {
    for (cgltf_size pi = 0; pi < mesh->primitives_count; ++pi) {
        const cgltf_primitive &prim = mesh->primitives[pi];
        if (prim.type != cgltf_primitive_type_triangles) continue;

        GltfMeshData md;
        md.name = mesh->name ? mesh->name : "mesh";
        if (prim.material)
            md.material_index = (int)(prim.material - data->materials);

        // Find accessors
        const cgltf_accessor *pos_acc     = nullptr;
        const cgltf_accessor *norm_acc    = nullptr;
        const cgltf_accessor *uv_acc      = nullptr;
        const cgltf_accessor *tan_acc     = nullptr;

        for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
            const cgltf_attribute &attr = prim.attributes[ai];
            switch (attr.type) {
                case cgltf_attribute_type_position: pos_acc     = attr.data; break;
                case cgltf_attribute_type_normal:   norm_acc    = attr.data; break;
                case cgltf_attribute_type_texcoord: uv_acc      = attr.data; break;
                case cgltf_attribute_type_tangent:  tan_acc     = attr.data; break;
                default: break;
            }
        }

        if (!pos_acc) continue;
        cgltf_size vert_count = pos_acc->count;
        md.vertices.resize(vert_count);

        // Extract positions
        for (cgltf_size vi = 0; vi < vert_count; ++vi) {
            float v[3] = {0, 0, 0};
            cgltf_accessor_read_float(pos_acc, vi, v, 3);
            md.vertices[vi].position = {v[0], v[1], v[2]};
        }

        // Extract normals
        if (norm_acc) {
            for (cgltf_size vi = 0; vi < vert_count; ++vi) {
                float v[3] = {0, 0, 1};
                cgltf_accessor_read_float(norm_acc, vi, v, 3);
                md.vertices[vi].normal = {v[0], v[1], v[2]};
            }
        } else {
            for (cgltf_size vi = 0; vi < vert_count; ++vi)
                md.vertices[vi].normal = {0.0f, 0.0f, 1.0f};
        }

        // Extract texcoords
        if (uv_acc) {
            for (cgltf_size vi = 0; vi < vert_count; ++vi) {
                float v[2] = {0, 0};
                cgltf_accessor_read_float(uv_acc, vi, v, 2);
                md.vertices[vi].texcoord = {v[0], v[1]};
            }
        } else {
            for (cgltf_size vi = 0; vi < vert_count; ++vi)
                md.vertices[vi].texcoord = {0.0f, 0.0f};
        }