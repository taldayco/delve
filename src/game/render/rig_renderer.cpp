#include "render/rig_renderer.h"
#include "rig.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>
#include <algorithm>

static constexpr float PI = 3.14159265358979323846f;

// ---- Isometric bone compensation ----
// The isometric view matrix scales Z by HS=12.5 vs XY by TW=2/TH=1.
// Vertical bones appear as 38:1 sticks without compensation.
struct IsoCompensation {
    float width_scale;    // multiply bone width (octahedrons only)
    float equator_t;      // equatorial ring position along bone
    float radial_z_comp;  // radial Z compression factor
};

static IsoCompensation iso_compensate_bone(const glm::vec3 &a, const glm::vec3 &b) {
    glm::vec3 dir = b - a;
    float len = glm::length(dir);
    if (len < 1e-5f) return {1.0f, 0.2f, 0.18f};
    dir /= len;

    float dot_z = glm::dot(dir, glm::vec3(0.0f, 0.0f, 1.0f));
    float z_align = fabsf(dot_z);

    IsoCompensation comp;
    comp.width_scale   = 1.0f + sqrtf(z_align) * 2.0f;          // 1.0 → 3.0
    comp.equator_t     = 0.2f + z_align * 0.15f;                 // 0.20 → 0.35
    comp.radial_z_comp = 0.18f + z_align * (1.0f - 0.18f);       // 0.18 → 1.0
    return comp;
}

void RigRenderer::init(SDL_GPUDevice *device,
                       SDL_GPUGraphicsPipeline *terrain_pipeline,
                       SDL_GPUBuffer *dummy_ssbo,
                       AssetManager *am) {
    if (initialized) return;
    gpu_device    = device;
    pipeline      = terrain_pipeline;
    dummy_ssbo_   = dummy_ssbo;
    asset_manager = am;

    if (!pipeline) return;

    // Static vertex buffer (max actors × vertices per actor).
    SDL_GPUBufferCreateInfo bi = {};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size  = MAX_RIG_VERTICES * sizeof(BasaltVertex);
    rig_vbo = SDL_CreateGPUBuffer(device, &bi);
    if (!rig_vbo) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "RigRenderer: Failed to create vertex buffer: %s", SDL_GetError());
        return;
    }

    // Persistent transfer buffer for zero-alloc uploads.
    SDL_GPUTransferBufferCreateInfo tbi = {};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size  = MAX_RIG_VERTICES * sizeof(BasaltVertex);
    transfer_buf = SDL_CreateGPUTransferBuffer(device, &tbi);
    if (!transfer_buf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "RigRenderer: Failed to create transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUBuffer(device, rig_vbo);
        rig_vbo = nullptr;
        return;
    }

    initialized = true;
    SDL_Log("RigRenderer: Initialized (VBO capacity: %u vertices)", MAX_RIG_VERTICES);
}

void RigRenderer::cleanup(SDL_GPUDevice *device) {
    if (rig_vbo)     { SDL_ReleaseGPUBuffer(device, rig_vbo);             rig_vbo     = nullptr; }
    if (transfer_buf) { SDL_ReleaseGPUTransferBuffer(device, transfer_buf); transfer_buf = nullptr; }
    // pipeline and dummy_ssbo_ are borrowed — not released here.
    pipeline    = nullptr;
    dummy_ssbo_ = nullptr;
    initialized = false;
}

void RigRenderer::emit_cylinder(const glm::vec3 &a, const glm::vec3 &b,
                                 float radius, glm::vec3 color, int sides,
                                 std::vector<BasaltVertex> &out_verts) {
    IsoCompensation cyl_comp = iso_compensate_bone(a, b);

    glm::vec3 up = b - a;
    float len = glm::length(up);
    if (len < 1e-5f) return;
    up /= len;

    glm::vec3 world_z(0.0f, 0.0f, 1.0f);
    glm::vec3 right;
    if (fabsf(glm::dot(up, world_z)) < 0.99f)
        right = glm::normalize(glm::cross(up, world_z));
    else
        right = glm::normalize(glm::cross(up, glm::vec3(1.0f, 0.0f, 0.0f)));
    glm::vec3 fwd = glm::cross(up, right);

    auto vert = [&](const glm::vec3 &pos, const glm::vec3 &n) {
        BasaltVertex v;
        v.pos_x   = pos.x;  v.pos_y   = pos.y;  v.pos_z   = pos.z;
        v.color_r = color.r; v.color_g = color.g; v.color_b = color.b;
        v.sheen   = 0.1f;
        v.nx = n.x; v.ny = n.y; v.nz = n.z;
        out_verts.push_back(v);
    };

    for (int i = 0; i < sides; ++i) {
        float angle0 = i       * (2.0f * PI / sides);
        float angle1 = (i + 1) * (2.0f * PI / sides);

        glm::vec3 r0 = (right * cosf(angle0) + fwd * sinf(angle0)) * radius;
        glm::vec3 r1 = (right * cosf(angle1) + fwd * sinf(angle1)) * radius;

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

void RigRenderer::emit_wireframe_box(const glm::vec3 &center, float half_size,
                                      float edge_radius, glm::vec3 color,
                                      std::vector<BasaltVertex> &out_verts) {
    float h = half_size;
    glm::vec3 c[8] = {
        {center.x - h, center.y - h, center.z - h},
        {center.x + h, center.y - h, center.z - h},
        {center.x + h, center.y + h, center.z - h},
        {center.x - h, center.y + h, center.z - h},
        {center.x - h, center.y - h, center.z + h},
        {center.x + h, center.y - h, center.z + h},
        {center.x + h, center.y + h, center.z + h},
        {center.x - h, center.y + h, center.z + h},
    };
    // 12 edges of a cube as thin cylinders
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},  // bottom face
        {4,5},{5,6},{6,7},{7,4},  // top face
        {0,4},{1,5},{2,6},{3,7},  // verticals
    };
    for (auto &e : edges)
        emit_cylinder(c[e[0]], c[e[1]], edge_radius, color, 4, out_verts);
}

uint32_t RigRenderer::prepare(SDL_GPUCommandBuffer *cmd, flecs::world &ecs) {
    if (!initialized || !rig_vbo || !transfer_buf) return 0;

    std::vector<BasaltVertex> verts;
    verts.reserve(1024);

    glm::vec3 body_color(0.55f, 0.52f, 0.48f);
    glm::vec3 ik_goal_color(0.2f, 0.8f, 0.2f);      // green for IK goals
    glm::vec3 pole_color(0.9f, 0.5f, 0.1f);          // orange for pole targets

    ecs.each([&](ActorTag, const RigPose &pose_in, const ActorConfig &cfg,
                 const Transform &t, const ProceduralGait &gait, const RigState &anim) {
        // Apply hip counter-animation on top of the already-posed skeleton.
        constexpr AnimationConfig anim_cfg{};
        RigHipState hip_anim;
        constexpr float TWO_PI = 2.0f * PI;
        float norm_phase = std::fmod(gait.phase, TWO_PI) / TWO_PI;
        if (norm_phase < 0.0f) norm_phase += 1.0f;
        hip_anim.stride_phase = norm_phase;
        compute_rig_hip_state(hip_anim, anim_cfg);

        // Build a local mutable copy of the pose to apply hip transform.
        RigPose pose = pose_in;

        // Vertical double-bounce: lift hips and upper legs.
        pose.joints[(int)Joint::HIPS].z       += hip_anim.hip_bob_y;
        pose.joints[(int)Joint::SPINE_01].z   += hip_anim.hip_bob_y * 0.5f;
        pose.joints[(int)Joint::L_UPPER_LEG].z += hip_anim.hip_bob_y;
        pose.joints[(int)Joint::R_UPPER_LEG].z += hip_anim.hip_bob_y;

        // Lateral drop: shift CoM toward stance side.
        float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);
        float drop_shift = hip_anim.hip_drop_fraction;
        glm::vec3 drop_vec(rght_x * drop_shift, rght_y * drop_shift, 0.0f);
        float side = (hip_anim.hip_rotation_deg >= 0.0f) ? 1.0f : -1.0f;
        pose.joints[(int)Joint::HIPS]        += drop_vec * side;
        pose.joints[(int)Joint::L_UPPER_LEG] += drop_vec * side;
        pose.joints[(int)Joint::R_UPPER_LEG] += drop_vec * side;

        // Apply isometric height foreshortening.
        {
            float foot_z = t.z - cfg.leg_len - cfg.shin_len;
            for (int ji = 0; ji < (int)Joint::COUNT; ++ji) {
                pose.joints[ji].z = foot_z + (pose.joints[ji].z - foot_z) * AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
            }
        }

        using J = Joint;
        auto j = [&](J jt) -> const glm::vec3 & { return pose.joints[(int)jt]; };

        // ---- Ground traces: hip + foot projections to ground plane ----
        {
            glm::vec3 trace_color(0.3f, 0.85f, 0.85f);  // teal
            float ground_z = j(J::ROOT).z;

            // Hip trace
            glm::vec3 hip_ground(j(J::HIPS).x, j(J::HIPS).y, ground_z);
            emit_cylinder(j(J::HIPS), hip_ground, 0.012f, trace_color, 4, verts);
            emit_flat_circle(hip_ground, cfg.torso_radius * 1.5f, trace_color, 16, verts);

            // Left foot trace
            glm::vec3 lfoot_ground(j(J::L_FOOT).x, j(J::L_FOOT).y, ground_z);
            emit_cylinder(j(J::L_FOOT), lfoot_ground, 0.008f, trace_color, 4, verts);
            emit_flat_circle(lfoot_ground, cfg.leg_radius * 1.2f, trace_color, 12, verts);

            // Right foot trace
            glm::vec3 rfoot_ground(j(J::R_FOOT).x, j(J::R_FOOT).y, ground_z);
            emit_cylinder(j(J::R_FOOT), rfoot_ground, 0.008f, trace_color, 4, verts);
            emit_flat_circle(rfoot_ground, cfg.leg_radius * 1.2f, trace_color, 12, verts);
        }

        // ---- Spine / torso chain — bone-shaped octahedrons ----
        emit_bone_oct(j(J::HIPS),     j(J::SPINE_01), cfg.torso_radius,        body_color, verts);
        emit_bone_oct(j(J::SPINE_01), j(J::SPINE_02), cfg.torso_radius,        body_color, verts);
        emit_bone_oct(j(J::SPINE_02), j(J::CHEST),    cfg.torso_radius,        body_color, verts);
        emit_bone_oct(j(J::CHEST),    j(J::NECK),     cfg.head_radius * 0.55f, body_color, verts);
        emit_bone_oct(j(J::NECK),     j(J::HEAD),     cfg.head_radius * 0.55f, body_color, verts);

        // Head skull: full octahedron at HEAD position + skull-top nub
        emit_diamond(j(J::HEAD), cfg.head_radius, body_color, verts);
        emit_bone_oct(j(J::HEAD), j(J::HEAD_END), cfg.head_radius * 0.4f, body_color, verts);

        // ---- Left arm chain (with clavicle) ----
        emit_bone_oct(j(J::CHEST),       j(J::L_CLAVICLE),  cfg.arm_radius * 1.4f,  body_color, verts);
        emit_bone_oct(j(J::L_CLAVICLE),  j(J::L_UPPER_ARM), cfg.arm_radius * 1.4f,  body_color, verts);
        emit_bone_oct(j(J::L_UPPER_ARM), j(J::L_LOWER_ARM), cfg.arm_radius,         body_color, verts);
        emit_bone_oct(j(J::L_LOWER_ARM), j(J::L_HAND),      cfg.arm_radius * 0.75f, body_color, verts);

        // ---- Right arm chain (with clavicle) ----
        emit_bone_oct(j(J::CHEST),       j(J::R_CLAVICLE),  cfg.arm_radius * 1.4f,  body_color, verts);
        emit_bone_oct(j(J::R_CLAVICLE),  j(J::R_UPPER_ARM), cfg.arm_radius * 1.4f,  body_color, verts);
        emit_bone_oct(j(J::R_UPPER_ARM), j(J::R_LOWER_ARM), cfg.arm_radius,         body_color, verts);
        emit_bone_oct(j(J::R_LOWER_ARM), j(J::R_HAND),      cfg.arm_radius * 0.75f, body_color, verts);

        // ---- Left leg chain (with toe) ----
        emit_bone_oct(j(J::HIPS),        j(J::L_UPPER_LEG), cfg.leg_radius * 1.1f,  body_color, verts);
        emit_bone_oct(j(J::L_UPPER_LEG), j(J::L_LOWER_LEG), cfg.leg_radius,         body_color, verts);
        emit_bone_oct(j(J::L_LOWER_LEG), j(J::L_FOOT),      cfg.leg_radius,         body_color, verts);
        emit_bone_oct(j(J::L_FOOT),      j(J::L_TOE),       cfg.leg_radius * 0.6f,  body_color, verts);

        // ---- Right leg chain (with toe) ----
        emit_bone_oct(j(J::HIPS),        j(J::R_UPPER_LEG), cfg.leg_radius * 1.1f,  body_color, verts);
        emit_bone_oct(j(J::R_UPPER_LEG), j(J::R_LOWER_LEG), cfg.leg_radius,         body_color, verts);
        emit_bone_oct(j(J::R_LOWER_LEG), j(J::R_FOOT),      cfg.leg_radius,         body_color, verts);
        emit_bone_oct(j(J::R_FOOT),      j(J::R_TOE),       cfg.leg_radius * 0.6f,  body_color, verts);

        // ---- Hierarchy V/A frames: shoulder girdle + pelvis bar ----
        emit_bone_oct(j(J::L_UPPER_ARM), j(J::R_UPPER_ARM), cfg.arm_radius * 1.0f, body_color, verts);
        emit_bone_oct(j(J::L_UPPER_LEG), j(J::R_UPPER_LEG), cfg.leg_radius * 1.2f, body_color, verts);

        // ---- Joint pivot spheres + RGB axis tripods (structural joints 0–23) ----
        {
            glm::vec3 joint_color(0.88f, 0.88f, 0.88f);
            constexpr float SPHERE_R  = 0.035f;
            constexpr float TRIPOD_SZ = 0.085f;
            for (int ji = 0; ji < (int)J::POLE_KNEE_L; ++ji) {
                emit_sphere(pose.joints[ji], SPHERE_R, joint_color, 6, 4, verts);
                emit_tripod(pose.joints[ji], TRIPOD_SZ, verts);
            }
        }

        // ---- IK goal wireframe boxes (green) ----
        float box_h = 0.04f;
        float edge_r = 0.008f;
        emit_wireframe_box(j(J::IK_FOOT_L), box_h, edge_r, ik_goal_color, verts);
        emit_wireframe_box(j(J::IK_FOOT_R), box_h, edge_r, ik_goal_color, verts);
        emit_wireframe_box(j(J::IK_HAND_L), box_h, edge_r, ik_goal_color, verts);
        emit_wireframe_box(j(J::IK_HAND_R), box_h, edge_r, ik_goal_color, verts);

        // ---- Pole target diamonds (orange) ----
        float diamond_r = 0.04f;
        emit_diamond(j(J::POLE_KNEE_L),  diamond_r, pole_color, verts);
        emit_diamond(j(J::POLE_KNEE_R),  diamond_r, pole_color, verts);
        emit_diamond(j(J::POLE_ELBOW_L), diamond_r, pole_color, verts);
        emit_diamond(j(J::POLE_ELBOW_R), diamond_r, pole_color, verts);

        // ---- Pole target lines (thin dashed cylinders from joint to pole) ----
        // Dashed: emit short segments alternating every 0.04f units along the line.
        auto emit_dashed = [&](const glm::vec3 &from, const glm::vec3 &to) {
            glm::vec3 dir = to - from;
            float total = glm::length(dir);
            if (total < 1e-5f) return;
            dir /= total;
            constexpr float seg_len = 0.04f;
            float t_cur = 0.0f;
            bool draw = true; // alternate on/off
            while (t_cur < total) {
                float t_next = std::min(t_cur + seg_len, total);
                if (draw) {
                    glm::vec3 seg_a = from + dir * t_cur;
                    glm::vec3 seg_b = from + dir * t_next;
                    emit_cylinder(seg_a, seg_b, 0.01f, pole_color, 4, verts);
                }
                t_cur = t_next;
                draw = !draw;
            }
        };

        emit_dashed(j(J::L_LOWER_LEG), j(J::POLE_KNEE_L));
        emit_dashed(j(J::R_LOWER_LEG), j(J::POLE_KNEE_R));
        emit_dashed(j(J::L_LOWER_ARM), j(J::POLE_ELBOW_L));
        emit_dashed(j(J::R_LOWER_ARM), j(J::POLE_ELBOW_R));
    });

    if (verts.empty()) return 0;

    uint32_t count = (uint32_t)std::min(verts.size(), (size_t)MAX_RIG_VERTICES);
    uint32_t byte_size = count * (uint32_t)sizeof(BasaltVertex);

    // Map transfer buffer, copy geometry.
    void *ptr = SDL_MapGPUTransferBuffer(gpu_device, transfer_buf, true);
    if (!ptr) return 0;
    std::memcpy(ptr, verts.data(), byte_size);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buf);

    // Copy transfer → vertex buffer.
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { transfer_buf, 0 };
    SDL_GPUBufferRegion           dst = { rig_vbo, 0, byte_size };
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);

    return count;
}

void RigRenderer::draw(SDL_GPURenderPass *pass,
                        SDL_GPUCommandBuffer *cmd,
                        const SceneUniforms &uniforms,
                        SDL_GPUBuffer *point_light_ssbo,
                        uint32_t vertex_count) {
    if (!initialized || !pipeline || !rig_vbo || vertex_count == 0) return;

    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    SDL_PushGPUVertexUniformData(cmd,   0, &uniforms, sizeof(SceneUniforms));
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));

    SDL_GPUBuffer *frag_storage[3] = {
        point_light_ssbo ? point_light_ssbo : dummy_ssbo_,
        dummy_ssbo_,
        dummy_ssbo_,
    };
    SDL_BindGPUFragmentStorageBuffers(pass, 0, frag_storage, 3);

    SDL_GPUBufferBinding vbind = { rig_vbo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);

    SDL_DrawGPUPrimitives(pass, vertex_count, 1, 0, 0);
}
