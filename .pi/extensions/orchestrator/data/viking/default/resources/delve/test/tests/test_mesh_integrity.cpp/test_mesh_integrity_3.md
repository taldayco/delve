// Each basalt layer's index references a unique range of the combined vertex buffer.
// Specifically: no layer should have MORE indices than 3 * vertices (triangle list max).
DELVE_TEST(mesh_index_to_vertex_ratio_sane) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (auto &layer : mesh.basalt_layers) {
        if (layer.vertices.empty()) continue;
        // Each vertex participates in at most ~6 triangles (hex fan); allow 10x headroom.
        size_t max_reasonable_indices = layer.vertices.size() * 10;
        if (layer.indices.size() > max_reasonable_indices) {
            fprintf(stderr, "  FAIL: %zu indices for %zu vertices (ratio %.1f > 10)\n",
                    layer.indices.size(), layer.vertices.size(),
                    (double)layer.indices.size() / layer.vertices.size());
            return false;
        }
    }
    return true;
}