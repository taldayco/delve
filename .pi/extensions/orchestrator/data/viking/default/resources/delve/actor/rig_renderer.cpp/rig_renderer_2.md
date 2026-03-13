// Direction-dependent radial Z compensation: vertical cylinders
        // need less squashing than horizontal ones under isometric projection.
        r0.z *= cyl_comp.radial_z_comp;
        r1.z *= cyl_comp.radial_z_comp;

        glm::vec3 p00 = a + r0;
        glm::vec3 p10 = a + r1;
        glm::vec3 p01 = b + r0;
        glm::vec3 p11 = b + r1;

        glm::vec3 face_n = glm::normalize(glm::cross(p10 - p00, p01 - p00));

        // Side quad.
        vert(p00, face_n); vert(p10, face_n); vert(p01, face_n);
        vert(p10, face_n); vert(p11, face_n); vert(p01, face_n);

        // Bottom cap.
        glm::vec3 bot_n = -up;
        vert(a, bot_n); vert(a + r1, bot_n); vert(a + r0, bot_n);

        // Top cap.
        glm::vec3 top_n = up;
        vert(b, top_n); vert(b + r0, top_n); vert(b + r1, top_n);
    }
}

// Bone-shaped octahedron: point at tail (a), equatorial ring of `width` radius at
// 20% of bone length, tapering to a point at tip (b). Square cross-section (4 sides).
// Same ISO_RADIAL_Z_COMP as emit_cylinder for visual consistency.
void RigRenderer::emit_bone_oct(const glm::vec3 &a, const glm::vec3 &b,
                                 float width, glm::vec3 color,
                                 std::vector<BasaltVertex> &out_verts) {
    IsoCompensation comp = iso_compensate_bone(a, b);

    glm::vec3 dir = b - a;
    float len = glm::length(dir);
    if (len < 1e-5f) return;
    dir /= len;

    glm::vec3 world_z(0.0f, 0.0f, 1.0f);
    glm::vec3 right;
    if (fabsf(glm::dot(dir, world_z)) < 0.99f)
        right = glm::normalize(glm::cross(dir, world_z));
    else
        right = glm::normalize(glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f)));
    glm::vec3 fwd = glm::cross(dir, right);

    constexpr int SIDES = 4;
    float effective_width = width * comp.width_scale;

    glm::vec3 ring[SIDES];
    glm::vec3 eq_center = a + dir * (len * comp.equator_t);
    for (int i = 0; i < SIDES; ++i) {
        float angle = i * (2.0f * PI / SIDES);
        glm::vec3 off = (right * cosf(angle) + fwd * sinf(angle)) * effective_width;
        off.z *= comp.radial_z_comp;
        ring[i] = eq_center + off;
    }

    auto push_tri = [&](const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2) {
        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        for (const auto &p : {p0, p1, p2}) {
            BasaltVertex v;
            v.pos_x = p.x; v.pos_y = p.y; v.pos_z = p.z;
            v.color_r = color.r; v.color_g = color.g; v.color_b = color.b;
            v.sheen = 0.2f;
            v.nx = n.x; v.ny = n.y; v.nz = n.z;
            out_verts.push_back(v);
        }
    };

    // Lower pyramid: tail point (a) to equatorial ring — outward winding.
    for (int i = 0; i < SIDES; ++i)
        push_tri(a, ring[(i + 1) % SIDES], ring[i]);

    // Upper pyramid: equatorial ring to tip point (b) — outward winding.
    for (int i = 0; i < SIDES; ++i)
        push_tri(b, ring[i], ring[(i + 1) % SIDES]);
}

// UV sphere for joint pivot markers.
// `sectors` = longitude divisions, `rings` = latitude divisions.
void RigRenderer::emit_sphere(const glm::vec3 &center, float radius, glm::vec3 color,
                               int sectors, int rings,
                               std::vector<BasaltVertex> &out_verts) {
    auto sphere_pt = [&](int lat, int lon) -> glm::vec3 {
        float phi   = PI * (float)lat  / (float)rings;
        float theta = 2.0f * PI * (float)lon / (float)sectors;
        float sp = sinf(phi), cp = cosf(phi);
        float st = sinf(theta), ct = cosf(theta);
        return center + glm::vec3(radius * sp * ct, radius * sp * st, radius * cp);
    };

    auto push_tri = [&](const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2) {
        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        for (const auto &p : {p0, p1, p2}) {
            BasaltVertex v;
            v.pos_x = p.x; v.pos_y = p.y; v.pos_z = p.z;
            v.color_r = color.r; v.color_g = color.g; v.color_b = color.b;
            v.sheen = 0.3f;
            v.nx = n.x; v.ny = n.y; v.nz = n.z;
            out_verts.push_back(v);
        }
    };

    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            glm::vec3 p00 = sphere_pt(r,   s);
            glm::vec3 p10 = sphere_pt(r,   s + 1);
            glm::vec3 p01 = sphere_pt(r + 1, s);
            glm::vec3 p11 = sphere_pt(r + 1, s + 1);
            push_tri(p00, p10, p01);
            push_tri(p10, p11, p01);
        }
    }
}