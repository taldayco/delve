#include "core/gltf_loader.h"
#include "core/skinned_mesh.h"
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
                          std::vector<GltfMeshData> &out, int skin_index = -1) {
    for (cgltf_size pi = 0; pi < mesh->primitives_count; ++pi) {
        const cgltf_primitive &prim = mesh->primitives[pi];
        if (prim.type != cgltf_primitive_type_triangles) continue;

        GltfMeshData md;
        md.name = mesh->name ? mesh->name : "mesh";
        if (prim.material)
            md.material_index = (int)(prim.material - data->materials);
        md.skin_index = skin_index;

        // Find accessors
        const cgltf_accessor *pos_acc     = nullptr;
        const cgltf_accessor *norm_acc    = nullptr;
        const cgltf_accessor *uv_acc      = nullptr;
        const cgltf_accessor *tan_acc     = nullptr;
        const cgltf_accessor *joints_acc  = nullptr;
        const cgltf_accessor *weights_acc = nullptr;

        for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
            const cgltf_attribute &attr = prim.attributes[ai];
            switch (attr.type) {
                case cgltf_attribute_type_position: pos_acc     = attr.data; break;
                case cgltf_attribute_type_normal:   norm_acc    = attr.data; break;
                case cgltf_attribute_type_texcoord: uv_acc      = attr.data; break;
                case cgltf_attribute_type_tangent:  tan_acc     = attr.data; break;
                case cgltf_attribute_type_joints:   joints_acc  = attr.data; break;
                case cgltf_attribute_type_weights:  weights_acc = attr.data; break;
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

        // Extract JOINTS_0 / WEIGHTS_0
        if (joints_acc && weights_acc) {
            md.joint_indices.resize(vert_count, glm::u8vec4(0));
            md.weights.resize(vert_count, glm::vec4(0.0f));

            for (cgltf_size vi = 0; vi < vert_count; ++vi) {
                cgltf_uint j[4] = {0, 0, 0, 0};
                cgltf_accessor_read_uint(joints_acc, vi, j, 4);
                md.joint_indices[vi] = glm::u8vec4(
                    (uint8_t)(j[0] > 255 ? 0 : j[0]),
                    (uint8_t)(j[1] > 255 ? 0 : j[1]),
                    (uint8_t)(j[2] > 255 ? 0 : j[2]),
                    (uint8_t)(j[3] > 255 ? 0 : j[3]));

                float w[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(weights_acc, vi, w, 4);
                float sum = w[0] + w[1] + w[2] + w[3];
                if (sum > 1e-6f) {
                    float inv = 1.0f / sum;
                    w[0] *= inv; w[1] *= inv; w[2] *= inv; w[3] *= inv;
                } else {
                    w[0] = 1.0f; w[1] = 0.0f; w[2] = 0.0f; w[3] = 0.0f;
                    md.joint_indices[vi] = glm::u8vec4(0);
                }
                md.weights[vi] = glm::vec4(w[0], w[1], w[2], w[3]);
            }
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

    // Extract meshes via node traversal (captures skin association)
    std::vector<bool> mesh_extracted(data->meshes_count, false);
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node &node = data->nodes[ni];
        if (!node.mesh) continue;
        int mesh_idx = (int)(node.mesh - data->meshes);
        if (mesh_idx < 0 || mesh_idx >= (int)data->meshes_count) continue;
        if (mesh_extracted[mesh_idx]) continue;
        mesh_extracted[mesh_idx] = true;

        int skin_index = -1;
        if (node.skin) skin_index = (int)(node.skin - data->skins);
        extract_mesh(node.mesh, data, asset.meshes, skin_index);
    }
    // Fallback: extract any meshes not referenced by nodes
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        if (!mesh_extracted[i])
            extract_mesh(&data->meshes[i], data, asset.meshes, -1);
    }

    // Extract skins (skeleton hierarchies + inverse bind matrices)
    for (cgltf_size si = 0; si < data->skins_count; ++si) {
        const cgltf_skin &skin = data->skins[si];
        GltfSkinData sd;

        // Read inverse bind matrices
        if (skin.inverse_bind_matrices) {
            sd.inverse_bind_matrices.resize(skin.joints_count);
            for (cgltf_size ji = 0; ji < skin.joints_count; ++ji) {
                float m[16];
                cgltf_accessor_read_float(skin.inverse_bind_matrices, ji, m, 16);
                // Column-major: m[0..3] = col0, m[4..7] = col1, etc.
                sd.inverse_bind_matrices[ji] = glm::mat4(
                    m[0], m[1], m[2], m[3],
                    m[4], m[5], m[6], m[7],
                    m[8], m[9], m[10], m[11],
                    m[12], m[13], m[14], m[15]);
            }
        } else {
            sd.inverse_bind_matrices.resize(skin.joints_count, glm::mat4(1.0f));
        }

        // Build joint hierarchy
        sd.joints.resize(skin.joints_count);
        sd.gltf_node_indices.resize(skin.joints_count);

        for (cgltf_size ji = 0; ji < skin.joints_count; ++ji) {
            const cgltf_node *node = skin.joints[ji];
            sd.gltf_node_indices[ji] = (int32_t)(node - data->nodes);

            GltfSkinJoint &joint = sd.joints[ji];
            joint.name = node->name ? node->name : "";

            if (node->has_translation) {
                joint.translation = {node->translation[0], node->translation[1], node->translation[2]};
            }
            if (node->has_rotation) {
                joint.rotation = {node->rotation[0], node->rotation[1],
                                  node->rotation[2], node->rotation[3]};
            }
            if (node->has_scale) {
                joint.scale = {node->scale[0], node->scale[1], node->scale[2]};
            }

            // Find parent: check if this node's parent is also a joint in this skin
            joint.parent_index = -1;
            if (node->parent) {
                int32_t parent_node_idx = (int32_t)(node->parent - data->nodes);
                for (cgltf_size pk = 0; pk < skin.joints_count; ++pk) {
                    if (sd.gltf_node_indices[pk] == parent_node_idx) {
                        joint.parent_index = (int32_t)pk;
                        break;
                    }
                }
            }
        }

        asset.skins.push_back(std::move(sd));
    }

    cgltf_free(data);
    asset.ok = true;

    SDL_Log("GltfLoader: Loaded '%s' (%zu meshes, %zu textures, %zu skins)",
            path.c_str(), asset.meshes.size(), asset.textures.size(), asset.skins.size());
    return asset;
}

SkinnedMeshData build_skinned_mesh(const GltfMeshData &mesh, const GltfSkinData &skin) {
    SkinnedMeshData out;
    if (!mesh.has_skinning()) {
        SDL_Log("build_skinned_mesh: mesh '%s' has no skinning data", mesh.name.c_str());
        return out;
    }

    size_t vert_count = mesh.vertices.size();
    out.vertices.resize(vert_count);
    out.indices = mesh.indices;

    glm::vec3 aabb_min(+1e30f), aabb_max(-1e30f);

    for (size_t i = 0; i < vert_count; ++i) {
        const GltfVertex &gv = mesh.vertices[i];
        const glm::u8vec4 &ji = mesh.joint_indices[i];
        const glm::vec4 &wt = mesh.weights[i];
        SkinnedVertex &sv = out.vertices[i];

        sv.pos_x = gv.position.x; sv.pos_y = gv.position.y; sv.pos_z = gv.position.z;
        sv.nx = gv.normal.x; sv.ny = gv.normal.y; sv.nz = gv.normal.z;
        sv.u = gv.texcoord.x; sv.v = gv.texcoord.y;
        sv.color_r = 0.8f; sv.color_g = 0.8f; sv.color_b = 0.8f;
        sv.roughness = 0.5f;
        sv.metallic = 0.0f;
        sv.joint_indices = pack_joint_indices(ji.x, ji.y, ji.z, ji.w);
        sv.weight_x = wt.x; sv.weight_y = wt.y; sv.weight_z = wt.z; sv.weight_w = wt.w;

        aabb_min = glm::min(aabb_min, gv.position);
        aabb_max = glm::max(aabb_max, gv.position);
    }

    out.aabb_min = aabb_min;
    out.aabb_max = aabb_max;
    out.inverse_bind_matrices = skin.inverse_bind_matrices;
    out.joint_count = (uint32_t)skin.joints.size();

    SDL_Log("build_skinned_mesh: '%s' -> %zu verts, %zu tris, %u joints",
            mesh.name.c_str(), vert_count, mesh.indices.size() / 3, out.joint_count);
    return out;
}
