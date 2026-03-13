// No NaN or Inf in basalt vertex positions.
DELVE_TEST(mesh_basalt_vertex_positions_finite) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (auto &layer : mesh.basalt_layers) {
        for (auto &v : layer.vertices) {
            EXPECT_FALSE(std::isnan(v.pos_x) || std::isinf(v.pos_x));
            EXPECT_FALSE(std::isnan(v.pos_y) || std::isinf(v.pos_y));
            EXPECT_FALSE(std::isnan(v.pos_z) || std::isinf(v.pos_z));
        }
    }
    return true;
}

// No NaN or Inf in lava vertex positions.
DELVE_TEST(mesh_lava_vertex_positions_finite) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (auto &v : mesh.lava_vertices) {
        EXPECT_FALSE(std::isnan(v.pos_x) || std::isinf(v.pos_x));
        EXPECT_FALSE(std::isnan(v.pos_y) || std::isinf(v.pos_y));
        EXPECT_FALSE(std::isnan(v.pos_z) || std::isinf(v.pos_z));
    }
    return true;
}

// Contour vertex positions must be finite.
DELVE_TEST(mesh_contour_vertex_positions_finite) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    for (auto &v : mesh.contour_vertices) {
        EXPECT_FALSE(std::isnan(v.pos_x) || std::isinf(v.pos_x));
        EXPECT_FALSE(std::isnan(v.pos_y) || std::isinf(v.pos_y));
        EXPECT_FALSE(std::isnan(v.pos_z) || std::isinf(v.pos_z));
    }
    return true;
}

// Basalt vertex positions should be within reasonable map bounds.
// Map is MW x MH pixels; world coords = pixel / HEX_SIZE.
// Allow 10% margin beyond the map edge.
DELVE_TEST(mesh_basalt_vertex_positions_within_map_bounds) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    float max_wx = (float)MW / Config::HEX_SIZE * 1.1f;
    float max_wy = (float)MH / Config::HEX_SIZE * 1.1f;
    // Hex columns can extend below 0 slightly for off-center grids.
    float min_wx = -(float)MW / Config::HEX_SIZE * 0.1f;
    float min_wy = -(float)MH / Config::HEX_SIZE * 0.1f;
    for (auto &layer : mesh.basalt_layers) {
        for (auto &v : layer.vertices) {
            if (v.pos_x < min_wx || v.pos_x > max_wx ||
                v.pos_y < min_wy || v.pos_y > max_wy) {
                fprintf(stderr, "  FAIL: vertex pos (%.3f, %.3f) outside map bounds"
                        " [%.1f..%.1f, %.1f..%.1f]\n",
                        v.pos_x, v.pos_y, min_wx, max_wx, min_wy, max_wy);
                return false;
            }
        }
    }
    return true;
}

// ── GPU buffer size limits ────────────────────────────────────────────────────

// Total basalt vertex count must not exceed what GPU can handle in one draw.
// Vulkan maxVertexInputAttributes typically limits usable verts to ~16M per draw;
// we use a conservative 4M to catch runaway column generation early.
DELVE_TEST(mesh_basalt_vertex_count_gpu_safe) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    constexpr size_t GPU_VERTEX_LIMIT = 4'000'000;
    size_t total = mesh_vertex_count(mesh);
    if (total > GPU_VERTEX_LIMIT) {
        fprintf(stderr, "  FAIL: basalt vertex count %zu exceeds GPU-safe limit %zu\n",
                total, GPU_VERTEX_LIMIT);
        return false;
    }
    return true;
}

// Total basalt index count must not exceed GPU-safe limit.
DELVE_TEST(mesh_basalt_index_count_gpu_safe) {
    auto md   = make_map();
    auto mesh = make_mesh(md);
    constexpr size_t GPU_INDEX_LIMIT = 12'000'000;
    size_t total = mesh_index_count(mesh);
    if (total > GPU_INDEX_LIMIT) {
        fprintf(stderr, "  FAIL: basalt index count %zu exceeds GPU-safe limit %zu\n",
                total, GPU_INDEX_LIMIT);
        return false;
    }
    return true;
}

// ── Structural consistency ────────────────────────────────────────────────────

// If columns exist, the mesh must have non-zero vertex/index data.
DELVE_TEST(mesh_non_empty_when_columns_exist) {
    auto md   = make_map();
    if (md.columns.empty()) return true; // vacuously OK
    auto mesh = make_mesh(md);
    EXPECT_GT(mesh_vertex_count(mesh), (size_t)0);
    EXPECT_GT(mesh_index_count(mesh),  (size_t)0);
    return true;
}