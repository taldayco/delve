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

        // Extract tangents
        if (tan_acc) {
            for (cgltf_size vi = 0; vi < vert_count; ++vi) {
                float v[4] = {1, 0, 0, 1};
                cgltf_accessor_read_float(tan_acc, vi, v, 4);
                md.vertices[vi].tangent = {v[0], v[1], v[2], v[3]};
            }
        } else {
            for (cgltf_size vi = 0; vi < vert_count; ++vi)
                md.vertices[vi].tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        }

        // Extract indices
        if (prim.indices) {
            md.indices.resize(prim.indices->count);
            for (cgltf_size ii = 0; ii < prim.indices->count; ++ii)
                md.indices[ii] = (uint32_t)cgltf_accessor_read_index(prim.indices, ii);
        } else {
            md.indices.resize(vert_count);
            for (cgltf_size ii = 0; ii < vert_count; ++ii)
                md.indices[ii] = (uint32_t)ii;
        }

        out.push_back(std::move(md));
    }
}

// --- Main loader ---

GltfAsset load_gltf(const std::string &path) {
    GltfAsset asset;

    cgltf_options options = {};
    cgltf_data *data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);

    if (result != cgltf_result_success) {
        asset.error = "Failed to parse glTF file: " + path;
        return asset;
    }

    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        asset.error = "Failed to load glTF buffers: " + path;
        cgltf_free(data);
        return asset;
    }

    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        asset.error = "glTF validation failed: " + path;
        cgltf_free(data);
        return asset;
    }

    std::string base_dir = fs::path(path).parent_path().string();

    // Extract textures (images referenced by materials)
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image &img = data->images[i];

        // Determine sRGB from material references
        bool srgb = true;
        for (cgltf_size mi = 0; mi < data->materials_count; ++mi) {
            for (cgltf_size ti = 0; ti < data->textures_count; ++ti) {
                if (data->textures[ti].image == &img) {
                    srgb = is_srgb_texture(&data->materials[mi], &data->textures[ti]);
                }
            }
        }

        GltfTextureData tex;
        if (img.buffer_view) {
            tex = load_image_from_buffer(img.buffer_view, img.name, srgb);
        } else if (img.uri) {
            tex = load_image_from_file(img.uri, base_dir, img.name, srgb);
        }

        if (!tex.pixels.empty())
            asset.textures.push_back(std::move(tex));
    }

    // Extract meshes via node traversal
    std::vector<bool> mesh_extracted(data->meshes_count, false);
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node &node = data->nodes[ni];
        if (!node.mesh) continue;
        int mesh_idx = (int)(node.mesh - data->meshes);
        if (mesh_idx < 0 || mesh_idx >= (int)data->meshes_count) continue;
        if (mesh_extracted[mesh_idx]) continue;
        mesh_extracted[mesh_idx] = true;

        extract_mesh(node.mesh, data, asset.meshes);
    }
    // Fallback: extract any meshes not referenced by nodes
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        if (!mesh_extracted[i])
            extract_mesh(&data->meshes[i], data, asset.meshes);
    }

    cgltf_free(data);
    asset.ok = true;

    SDL_Log("GltfLoader: Loaded '%s' (%zu meshes, %zu textures)",
            path.c_str(), asset.meshes.size(), asset.textures.size());
    return asset;
}
