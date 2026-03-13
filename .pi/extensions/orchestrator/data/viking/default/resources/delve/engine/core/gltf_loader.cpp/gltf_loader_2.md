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