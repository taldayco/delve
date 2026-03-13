#include "core/gltf_loader.h"
#include <cgltf.h>
#include <stb_image.h>
#include <SDL3/SDL_log.h>
#include <filesystem>
#include <unordered_map>

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

// --- Skinned mesh loader ---

GltfSkinnedAsset load_gltf_skinned(const std::string &path) {
    GltfSkinnedAsset asset;

    cgltf_options options = {};
    cgltf_data *data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        asset.error = "Failed to parse: " + path;
        return asset;
    }
    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        asset.error = "Failed to load buffers: " + path;
        cgltf_free(data); return asset;
    }

    // --- Parse skeleton from first skin ---
    if (data->skins_count > 0) {
        const cgltf_skin &skin = data->skins[0];
        int bone_count = (int)skin.joints_count;
        asset.skeleton.bones.resize(bone_count);

        // Build node->bone index map
        std::unordered_map<const cgltf_node *, int> node_to_bone;
        for (int i = 0; i < bone_count; ++i)
            node_to_bone[skin.joints[i]] = i;

        // Inverse bind matrices
        std::vector<float> ibm_data(bone_count * 16, 0.0f);
        if (skin.inverse_bind_matrices) {
            for (int i = 0; i < bone_count; ++i)
                cgltf_accessor_read_float(skin.inverse_bind_matrices, i, ibm_data.data() + i * 16, 16);
        }

        for (int i = 0; i < bone_count; ++i) {
            const cgltf_node *joint = skin.joints[i];
            GltfBone &bone = asset.skeleton.bones[i];
            bone.name = joint->name ? joint->name : "";

            // Parent index
            bone.parent_index = -1;
            if (joint->parent) {
                auto it = node_to_bone.find(joint->parent);
                if (it != node_to_bone.end())
                    bone.parent_index = it->second;
            }

            // Inverse bind matrix (column-major from glTF)
            float *m = ibm_data.data() + i * 16;
            bone.inverse_bind_matrix = glm::mat4(
                m[0], m[1], m[2], m[3],
                m[4], m[5], m[6], m[7],
                m[8], m[9], m[10], m[11],
                m[12], m[13], m[14], m[15]);

            // Local rest transform
            glm::mat4 local(1.0f);
            if (joint->has_matrix) {
                const float *lm = joint->matrix;
                local = glm::mat4(
                    lm[0], lm[1], lm[2], lm[3],
                    lm[4], lm[5], lm[6], lm[7],
                    lm[8], lm[9], lm[10], lm[11],
                    lm[12], lm[13], lm[14], lm[15]);
            } else {
                glm::vec3 t(0.0f), s(1.0f);
                glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
                if (joint->has_translation) t = {joint->translation[0], joint->translation[1], joint->translation[2]};
                if (joint->has_rotation)    r = glm::quat(joint->rotation[3], joint->rotation[0], joint->rotation[1], joint->rotation[2]);
                if (joint->has_scale)       s = {joint->scale[0], joint->scale[1], joint->scale[2]};
                local = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
            }
            bone.local_rest_transform = local;
        }

        // Find root bone (bone with no parent in skin)
        asset.skeleton.root_bone_index = 0;
        for (int i = 0; i < bone_count; ++i) {
            if (asset.skeleton.bones[i].parent_index == -1) {
                asset.skeleton.root_bone_index = i;
                break;
            }
        }

        // Build flat inverse_bind_matrices array for GPU upload
        asset.inverse_bind_matrices.resize(bone_count);
        for (int i = 0; i < bone_count; ++i)
            asset.inverse_bind_matrices[i] = asset.skeleton.bones[i].inverse_bind_matrix;

        // --- Parse skinned meshes ---
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
            const cgltf_node &node = data->nodes[ni];
            if (!node.mesh) continue;

            for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
                const cgltf_primitive &prim = node.mesh->primitives[pi];
                if (prim.type != cgltf_primitive_type_triangles) continue;

                GltfSkinnedMeshData md;
                md.name = node.mesh->name ? node.mesh->name : "skinned_mesh";

                const cgltf_accessor *pos_acc    = nullptr;
                const cgltf_accessor *norm_acc   = nullptr;
                const cgltf_accessor *uv_acc     = nullptr;
                const cgltf_accessor *tan_acc    = nullptr;
                const cgltf_accessor *joint_acc  = nullptr;
                const cgltf_accessor *weight_acc = nullptr;

                for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                    const cgltf_attribute &attr = prim.attributes[ai];
                    switch (attr.type) {
                        case cgltf_attribute_type_position: pos_acc    = attr.data; break;
                        case cgltf_attribute_type_normal:   norm_acc   = attr.data; break;
                        case cgltf_attribute_type_texcoord: if (attr.index == 0) uv_acc = attr.data; break;
                        case cgltf_attribute_type_tangent:  tan_acc    = attr.data; break;
                        case cgltf_attribute_type_joints:   if (attr.index == 0) joint_acc  = attr.data; break;
                        case cgltf_attribute_type_weights:  if (attr.index == 0) weight_acc = attr.data; break;
                        default: break;
                    }
                }

                if (!pos_acc) continue;
                cgltf_size vc = pos_acc->count;
                md.vertices.resize(vc);

                for (cgltf_size vi = 0; vi < vc; ++vi) {
                    SkinnedVertex &sv = md.vertices[vi];
                    float v3[3] = {0,0,0}, v4[4] = {1,0,0,1}, v2[2] = {0,0};

                    cgltf_accessor_read_float(pos_acc, vi, v3, 3);
                    sv.position = {v3[0], v3[1], v3[2]};

                    if (norm_acc) { cgltf_accessor_read_float(norm_acc, vi, v3, 3); sv.normal = {v3[0], v3[1], v3[2]}; }
                    else sv.normal = {0,0,1};

                    if (uv_acc)  { cgltf_accessor_read_float(uv_acc, vi, v2, 2); sv.texcoord = {v2[0], v2[1]}; }
                    else sv.texcoord = {0,0};

                    if (tan_acc) { cgltf_accessor_read_float(tan_acc, vi, v4, 4); sv.tangent = {v4[0], v4[1], v4[2], v4[3]}; }
                    else sv.tangent = {1,0,0,1};

                    if (joint_acc) {
                        cgltf_uint ji[4] = {0,0,0,0};
                        cgltf_accessor_read_uint(joint_acc, vi, ji, 4);
                        sv.joints[0] = (uint8_t)ji[0]; sv.joints[1] = (uint8_t)ji[1];
                        sv.joints[2] = (uint8_t)ji[2]; sv.joints[3] = (uint8_t)ji[3];
                    } else { sv.joints[0]=sv.joints[1]=sv.joints[2]=sv.joints[3]=0; }

                    if (weight_acc) { cgltf_accessor_read_float(weight_acc, vi, sv.weights, 4); }
                    else { sv.weights[0]=1; sv.weights[1]=sv.weights[2]=sv.weights[3]=0; }
                }

                if (prim.indices) {
                    md.indices.resize(prim.indices->count);
                    for (cgltf_size ii = 0; ii < prim.indices->count; ++ii)
                        md.indices[ii] = (uint32_t)cgltf_accessor_read_index(prim.indices, ii);
                } else {
                    md.indices.resize(vc);
                    for (cgltf_size ii = 0; ii < vc; ++ii) md.indices[ii] = (uint32_t)ii;
                }

                asset.meshes.push_back(std::move(md));
            }
        }

        // --- Parse animations ---
        for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
            const cgltf_animation &anim = data->animations[ai];
            GltfAnimationClip clip;
            clip.name = anim.name ? anim.name : ("anim_" + std::to_string(ai));
            clip.duration = 0.0f;

            for (cgltf_size ci = 0; ci < anim.channels_count; ++ci) {
                const cgltf_animation_channel &ch = anim.channels[ci];
                if (!ch.target_node || !ch.sampler) continue;

                auto it = node_to_bone.find(ch.target_node);
                if (it == node_to_bone.end()) continue;

                GltfAnimChannel gc;
                gc.bone_index = it->second;

                // Read times
                const cgltf_accessor *times_acc = ch.sampler->input;
                gc.times.resize(times_acc->count);
                for (cgltf_size ti = 0; ti < times_acc->count; ++ti) {
                    cgltf_accessor_read_float(times_acc, ti, &gc.times[ti], 1);
                    clip.duration = std::max(clip.duration, gc.times[ti]);
                }

                const cgltf_accessor *vals_acc = ch.sampler->output;
                if (ch.target_path == cgltf_animation_path_type_translation) {
                    gc.path = "translation";
                    gc.translations.resize(times_acc->count);
                    for (cgltf_size ti = 0; ti < times_acc->count; ++ti) {
                        float v[3]; cgltf_accessor_read_float(vals_acc, ti, v, 3);
                        gc.translations[ti] = {v[0], v[1], v[2]};
                    }
                } else if (ch.target_path == cgltf_animation_path_type_rotation) {
                    gc.path = "rotation";
                    gc.rotations.resize(times_acc->count);
                    for (cgltf_size ti = 0; ti < times_acc->count; ++ti) {
                        float v[4]; cgltf_accessor_read_float(vals_acc, ti, v, 4);
                        // glTF [x,y,z,w] → glm::quat(w,x,y,z)
                        gc.rotations[ti] = glm::quat(v[3], v[0], v[1], v[2]);
                    }
                } else if (ch.target_path == cgltf_animation_path_type_scale) {
                    gc.path = "scale";
                    gc.scales.resize(times_acc->count);
                    for (cgltf_size ti = 0; ti < times_acc->count; ++ti) {
                        float v[3]; cgltf_accessor_read_float(vals_acc, ti, v, 3);
                        gc.scales[ti] = {v[0], v[1], v[2]};
                    }
                } else {
                    continue;
                }

                clip.channels.push_back(std::move(gc));
            }

            asset.animations.push_back(std::move(clip));
        }
    }

    // Parse root scene node transform (captures Blender Y→Z-up rotation, etc.)
    if (data->scene && data->scene->nodes_count > 0) {
        const cgltf_node *root_node = data->scene->nodes[0];
        if (root_node->has_matrix) {
            const float *lm = root_node->matrix;
            asset.root_transform = glm::mat4(
                lm[0], lm[1], lm[2],  lm[3],
                lm[4], lm[5], lm[6],  lm[7],
                lm[8], lm[9], lm[10], lm[11],
                lm[12],lm[13],lm[14], lm[15]);
        } else {
            glm::vec3 t(0.0f), s(1.0f);
            glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
            if (root_node->has_translation)
                t = {root_node->translation[0], root_node->translation[1], root_node->translation[2]};
            if (root_node->has_rotation)
                r = glm::quat(root_node->rotation[3], root_node->rotation[0], root_node->rotation[1], root_node->rotation[2]);
            if (root_node->has_scale)
                s = {root_node->scale[0], root_node->scale[1], root_node->scale[2]};
            asset.root_transform = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
        }
    }

    cgltf_free(data);
    asset.ok = true;
    SDL_Log("GltfSkinnedLoader: Loaded '%s' (%zu meshes, %zu bones, %zu anims)",
            path.c_str(), asset.meshes.size(),
            asset.skeleton.bones.size(), asset.animations.size());
    return asset;
}
