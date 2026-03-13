// RGB world-axis tripod: X=red, Y=green, Z=blue.
// Uses thin 4-sided cylinders of radius 0.005 * size.
void RigRenderer::emit_tripod(const glm::vec3 &center, float size,
                               std::vector<BasaltVertex> &out_verts) {
    float r = size * 0.055f; // cylinder radius proportional to arm length
    emit_cylinder(center, center + glm::vec3(size, 0.0f, 0.0f), r, {1.0f, 0.0f, 0.0f}, 4, out_verts);
    emit_cylinder(center, center + glm::vec3(0.0f, size, 0.0f), r, {0.0f, 1.0f, 0.0f}, 4, out_verts);
    emit_cylinder(center, center + glm::vec3(0.0f, 0.0f, size), r, {0.0f, 0.0f, 1.0f}, 4, out_verts);
}

// Emit a cube (axis-aligned box) centered at `center` with half_size per axis.
// Generates 6 faces × 2 triangles = 12 triangles.
void RigRenderer::emit_box(const glm::vec3 &center, float half_size, glm::vec3 color,
                            std::vector<BasaltVertex> &out_verts) {
    float h = half_size;
    glm::vec3 corners[8] = {
        {center.x - h, center.y - h, center.z - h},
        {center.x + h, center.y - h, center.z - h},
        {center.x + h, center.y + h, center.z - h},
        {center.x - h, center.y + h, center.z - h},
        {center.x - h, center.y - h, center.z + h},
        {center.x + h, center.y - h, center.z + h},
        {center.x + h, center.y + h, center.z + h},
        {center.x - h, center.y + h, center.z + h},
    };

    auto push_tri = [&](int a, int b, int c, const glm::vec3 &n) {
        for (int idx : {a, b, c}) {
            BasaltVertex v;
            v.pos_x = corners[idx].x; v.pos_y = corners[idx].y; v.pos_z = corners[idx].z;
            v.color_r = color.r; v.color_g = color.g; v.color_b = color.b;
            v.sheen = 0.05f;
            v.nx = n.x; v.ny = n.y; v.nz = n.z;
            out_verts.push_back(v);
        }
    };

    // -Z face
    push_tri(0, 2, 1, {0, 0, -1}); push_tri(0, 3, 2, {0, 0, -1});
    // +Z face
    push_tri(4, 5, 6, {0, 0, 1});  push_tri(4, 6, 7, {0, 0, 1});
    // -X face
    push_tri(0, 4, 7, {-1, 0, 0}); push_tri(0, 7, 3, {-1, 0, 0});
    // +X face
    push_tri(1, 2, 6, {1, 0, 0});  push_tri(1, 6, 5, {1, 0, 0});
    // -Y face
    push_tri(0, 1, 5, {0, -1, 0}); push_tri(0, 5, 4, {0, -1, 0});
    // +Y face
    push_tri(2, 3, 7, {0, 1, 0});  push_tri(2, 7, 6, {0, 1, 0});
}

// Emit an octahedron (diamond) centered at `center` with `radius`.
// 8 triangles from 6 axis-aligned vertices.
void RigRenderer::emit_diamond(const glm::vec3 &center, float radius, glm::vec3 color,
                                std::vector<BasaltVertex> &out_verts) {
    glm::vec3 px = center + glm::vec3( radius,  0,  0);
    glm::vec3 nx = center + glm::vec3(-radius,  0,  0);
    glm::vec3 py = center + glm::vec3( 0,  radius,  0);
    glm::vec3 ny = center + glm::vec3( 0, -radius,  0);
    glm::vec3 pz = center + glm::vec3( 0,  0,  radius);
    glm::vec3 nz = center + glm::vec3( 0,  0, -radius);

    auto push_tri = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        for (const auto &p : {a, b, c}) {
            BasaltVertex v;
            v.pos_x = p.x; v.pos_y = p.y; v.pos_z = p.z;
            v.color_r = color.r; v.color_g = color.g; v.color_b = color.b;
            v.sheen = 0.15f;
            v.nx = n.x; v.ny = n.y; v.nz = n.z;
            out_verts.push_back(v);
        }
    };

    // Top (+Z) hemisphere
    push_tri(pz, px, py);
    push_tri(pz, py, nx);
    push_tri(pz, nx, ny);
    push_tri(pz, ny, px);
    // Bottom (-Z) hemisphere
    push_tri(nz, py, px);
    push_tri(nz, nx, py);
    push_tri(nz, ny, nx);
    push_tri(nz, px, ny);
}

void RigRenderer::emit_flat_circle(const glm::vec3 &center, float radius, glm::vec3 color,
                                    int segments, std::vector<BasaltVertex> &out_verts) {
    glm::vec3 n(0.0f, 0.0f, 1.0f);  // up-facing normal
    auto vert = [&](const glm::vec3 &pos) {
        BasaltVertex v;
        v.pos_x = pos.x; v.pos_y = pos.y; v.pos_z = pos.z;
        v.color_r = color.r; v.color_g = color.g; v.color_b = color.b;
        v.sheen = 0.05f;
        v.nx = n.x; v.ny = n.y; v.nz = n.z;
        out_verts.push_back(v);
    };
    for (int i = 0; i < segments; ++i) {
        float a0 = i       * (2.0f * PI / segments);
        float a1 = (i + 1) * (2.0f * PI / segments);
        glm::vec3 p0(center.x + radius * cosf(a0), center.y + radius * sinf(a0), center.z);
        glm::vec3 p1(center.x + radius * cosf(a1), center.y + radius * sinf(a1), center.z);
        // CCW fan triangle viewed from +Z → outward normal in +Z
        vert(center); vert(p0); vert(p1);
    }
}