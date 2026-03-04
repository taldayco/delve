#include "skeleton_mesh.h"
#include "actor.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Bone table: (start_joint, end_joint) pairs, one per BoneSeg value.
// Order must match BoneSeg enum in actor_renderer.h.
// ---------------------------------------------------------------------------
static constexpr struct { int a, b; } BONES[] = {
    { (int)Joint::ROOT,       (int)Joint::SPINE      }, // SPINE
    { (int)Joint::SPINE,      (int)Joint::CHEST      }, // CHEST_CORE
    { (int)Joint::CHEST,      (int)Joint::NECK       }, // NECK_SEG
    { (int)Joint::NECK,       (int)Joint::HEAD       }, // HEAD_SEG
    { (int)Joint::CHEST,      (int)Joint::L_SHOULDER }, // L_SHOULDER_CONN
    { (int)Joint::L_SHOULDER, (int)Joint::L_ELBOW    }, // L_UPPER_ARM
    { (int)Joint::L_ELBOW,    (int)Joint::L_WRIST    }, // L_FOREARM
    { (int)Joint::CHEST,      (int)Joint::R_SHOULDER }, // R_SHOULDER_CONN
    { (int)Joint::R_SHOULDER, (int)Joint::R_ELBOW    }, // R_UPPER_ARM
    { (int)Joint::R_ELBOW,    (int)Joint::R_WRIST    }, // R_FOREARM
    { (int)Joint::SPINE,      (int)Joint::L_HIP      }, // L_HIP_CONN
    { (int)Joint::L_HIP,      (int)Joint::L_KNEE     }, // L_UPPER_LEG
    { (int)Joint::L_KNEE,     (int)Joint::L_ANKLE    }, // L_LOWER_LEG
    { (int)Joint::SPINE,      (int)Joint::R_HIP      }, // R_HIP_CONN
    { (int)Joint::R_HIP,      (int)Joint::R_KNEE     }, // R_UPPER_LEG
    { (int)Joint::R_KNEE,     (int)Joint::R_ANKLE    }, // R_LOWER_LEG
};
static constexpr int NUM_BONES = (int)(sizeof(BONES) / sizeof(BONES[0]));
static_assert(NUM_BONES == NUM_BONE_PROFILES, "BONES table must match NUM_BONE_PROFILES");

// Joints that receive an end-cap (extremities).
static constexpr int TERMINAL_JOINTS[] = {
    (int)Joint::HEAD,
    (int)Joint::L_WRIST,
    (int)Joint::R_WRIST,
    (int)Joint::L_ANKLE,
    (int)Joint::R_ANKLE,
};
static constexpr float PI = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Frenet-like bone frame: z = bone tangent, x/y = perpendicular plane.
// ---------------------------------------------------------------------------
struct BoneFrame {
    glm::vec3 origin;
    glm::vec3 x, y, z;
    float     length;
};

static BoneFrame make_frame(const glm::vec3 &a, const glm::vec3 &b) {
    BoneFrame f;
    f.origin = a;
    f.z      = b - a;
    f.length = glm::length(f.z);
    if (f.length > 1e-5f) f.z /= f.length;
    else                   f.z = glm::vec3(0.0f, 0.0f, 1.0f);

    glm::vec3 up(0.0f, 0.0f, 1.0f);
    if (fabsf(glm::dot(f.z, up)) > 0.99f)
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    f.x = glm::normalize(glm::cross(f.z, up));
    f.y = glm::cross(f.x, f.z);
    return f;
}

// World position → bone-local coords (position).
static glm::vec3 world_to_local(const glm::vec3 &wp, const BoneFrame &f) {
    glm::vec3 d = wp - f.origin;
    return { glm::dot(d, f.x), glm::dot(d, f.y), glm::dot(d, f.z) };
}

// World direction → bone-local coords (direction, no translation).
static glm::vec3 dir_to_local(const glm::vec3 &wd, const BoneFrame &f) {
    return { glm::dot(wd, f.x), glm::dot(wd, f.y), glm::dot(wd, f.z) };
}

// Bone-local position → world position.
static glm::vec3 local_to_world(const glm::vec3 &lp, const BoneFrame &f) {
    return f.origin + f.x * lp.x + f.y * lp.y + f.z * lp.z;
}

// Bone-local direction → world direction.
static glm::vec3 local_dir_to_world(const glm::vec3 &ld, const BoneFrame &f) {
    return f.x * ld.x + f.y * ld.y + f.z * ld.z;
}

// ---------------------------------------------------------------------------
// generate_skeleton_mesh
// Builds a proper triangle-list mesh with N-gon cross sections, caps, and
// a sphere-approximated head.
// ---------------------------------------------------------------------------
SkeletonMesh generate_skeleton_mesh(const SkeletonPose     &bind_pose,
                                    const BoneProfileArray &profiles) {
    SkeletonMesh mesh;
    mesh.vertices.reserve(300);
    mesh.indices.reserve(1500);
    mesh.rest_positions.reserve(300);
    mesh.rest_normals.reserve(300);

    // Helper: add a vertex and return its index.
    auto add_vert = [&](const glm::vec3 &wp, const glm::vec3 &wn,
                        const glm::vec3 &lp, const glm::vec3 &ln,
                        int bi, float weight) -> uint32_t {
        uint32_t idx = (uint32_t)mesh.vertices.size();
        SkeletonVertex sv;
        sv.position    = wp;
        sv.normal      = wn;
        sv.bone_index0 = (float)bi;
        sv.bone_weight = weight;
        // NOTE: bone_index1 always equals bone_index0 (single-bone influence per vertex).
        // Two-bone LBS blending at joint boundaries is not yet implemented; vertices near
        // joints crease sharply during deep bends. To implement, vertices at ring1 should
        // blend between bone bi and the next connected bone.
        sv.bone_index1 = (float)bi;
        mesh.vertices.push_back(sv);
        mesh.rest_positions.push_back(lp);
        mesh.rest_normals.push_back(ln);
        mesh.vertex_bone.push_back(bi);
        mesh.vertex_bone_weight.push_back(weight);
        return idx;
    };

    // Helper: emit a ring of N vertices around `center` with radius `r` using `frame`.
    // Returns base vertex index.
    auto emit_ring = [&](const glm::vec3 &center, float r,
                         const BoneFrame &frame, int sides,
                         int bi, float weight) -> uint32_t {
        uint32_t base = (uint32_t)mesh.vertices.size();
        for (int si = 0; si < sides; ++si) {
            float angle = si * (2.0f * PI / sides);
            glm::vec3 radial = frame.x * cosf(angle) + frame.y * sinf(angle);
            glm::vec3 wp = center + radial * r;
            glm::vec3 wn = (r > 1e-5f) ? radial : frame.z;
            glm::vec3 lp = world_to_local(wp, frame);
            glm::vec3 ln = dir_to_local(wn, frame);
            add_vert(wp, wn, lp, ln, bi, weight);
        }
        return base;
    };

    // Helper: connect two same-size rings with a triangle strip (2 triangles/side).
    auto connect_rings = [&](uint32_t r0, uint32_t r1, int sides) {
        for (int si = 0; si < sides; ++si) {
            int sn = (si + 1) % sides;
            uint32_t v00 = r0 + (uint32_t)si,  v10 = r0 + (uint32_t)sn;
            uint32_t v01 = r1 + (uint32_t)si,  v11 = r1 + (uint32_t)sn;
            mesh.indices.push_back(v00); mesh.indices.push_back(v01); mesh.indices.push_back(v10);
            mesh.indices.push_back(v10); mesh.indices.push_back(v01); mesh.indices.push_back(v11);
        }
    };

    // Helper: fan cap from `cap_idx` inward onto ring (outward=false → end cap facing +z).
    auto fan_cap = [&](uint32_t ring_base, int sides, uint32_t cap_idx, bool flip) {
        for (int si = 0; si < sides; ++si) {
            int sn = (si + 1) % sides;
            if (flip) {
                mesh.indices.push_back(ring_base + (uint32_t)si);
                mesh.indices.push_back(cap_idx);
                mesh.indices.push_back(ring_base + (uint32_t)sn);
            } else {
                mesh.indices.push_back(ring_base + (uint32_t)sn);
                mesh.indices.push_back(cap_idx);
                mesh.indices.push_back(ring_base + (uint32_t)si);
            }
        }
    };

    // Track bone 0's start ring for the root cap.
    uint32_t bone0_ring0 = 0;
    bool     bone0_ring0_set = false;

    for (int bi = 0; bi < NUM_BONES; ++bi) {
        int ja = BONES[bi].a;
        int jb = BONES[bi].b;

        const BoneProfile &prof = profiles[(size_t)bi];
        int   sides = std::max(3, prof.sides);
        float r0    = prof.radius_start;
        float r1    = prof.radius_end;
        float twist = prof.twist * (PI / 180.0f);

        const glm::vec3 &pa = bind_pose.joints[ja];
        const glm::vec3 &pb = bind_pose.joints[jb];

        BoneFrame frame = make_frame(pa, pb);

        // Apply twist rotation around bone axis.
        if (fabsf(twist) > 1e-5f) {
            float c = cosf(twist), s = sinf(twist);
            glm::vec3 nx = frame.x * c + frame.y * s;
            glm::vec3 ny = -frame.x * s + frame.y * c;
            frame.x = nx; frame.y = ny;
        }

        // Start ring (weight=1 → parent joint).
        uint32_t ring0 = emit_ring(pa, r0, frame, sides, bi, 1.0f);
        if (bi == 0 && !bone0_ring0_set) { bone0_ring0 = ring0; bone0_ring0_set = true; }

        // End ring (weight=0 → child joint).
        uint32_t ring1 = emit_ring(pb, r1, frame, sides, bi, 0.0f);

        // Connect with triangle strip.
        connect_rings(ring0, ring1, sides);

        // Terminal joint: add end cap.
        bool is_terminal = false;
        bool is_head     = (jb == (int)Joint::HEAD);
        for (int tj : TERMINAL_JOINTS)
            if (jb == tj) { is_terminal = true; break; }

        if (is_terminal) {
            if (is_head) {
                // Sphere approximation: 3 rings that rise and taper.
                // Ring parameters: (z_offset_from_pb, radius_scale, normal_up_blend)
                static constexpr float Z_OFF[3]    = { 0.10f, 0.22f, 0.32f };
                static constexpr float R_SCALE[3]  = { 1.2f,  1.0f,  0.6f  };
                static constexpr float UP_BLEND[3] = { 0.25f, 0.50f, 0.80f };

                uint32_t prev_ring = ring1;
                for (int ri = 0; ri < 3; ++ri) {
                    glm::vec3 center = pb + frame.z * Z_OFF[ri];
                    float rr = r1 * R_SCALE[ri];
                    uint32_t new_ring = (uint32_t)mesh.vertices.size();
                    for (int si = 0; si < sides; ++si) {
                        float angle = si * (2.0f * PI / sides);
                        glm::vec3 radial = frame.x * cosf(angle) + frame.y * sinf(angle);
                        glm::vec3 wp = center + radial * rr;
                        float ub = UP_BLEND[ri];
                        glm::vec3 wn = glm::normalize(radial * (1.0f - ub) + frame.z * ub);
                        glm::vec3 lp = world_to_local(wp, frame);
                        glm::vec3 ln = dir_to_local(wn, frame);
                        add_vert(wp, wn, lp, ln, bi, 0.0f);
                    }
                    connect_rings(prev_ring, new_ring, sides);
                    prev_ring = new_ring;
                }
                // Top-pole cap vertex.
                glm::vec3 top_wp = pb + frame.z * (Z_OFF[2] + r1 * R_SCALE[2] * 0.5f);
                glm::vec3 top_lp = world_to_local(top_wp, frame);
                glm::vec3 top_ln = glm::vec3(0.0f, 0.0f, 1.0f);
                uint32_t top_idx = add_vert(top_wp, frame.z, top_lp, top_ln, bi, 0.0f);
                fan_cap(prev_ring, sides, top_idx, false);
            } else {
                bool is_ankle = (jb == (int)Joint::L_ANKLE || jb == (int)Joint::R_ANKLE);
                if (is_ankle) {
                    // Foot elongation: extrude end ring forward 0.3 units along bone
                    // axis, then close with a flat cap.
                    static constexpr float FOOT_EXTEND = 0.3f;
                    glm::vec3 tip_center = pb + frame.z * FOOT_EXTEND;
                    uint32_t foot_ring = emit_ring(tip_center, r1, frame, sides, bi, 0.0f);
                    connect_rings(ring1, foot_ring, sides);
                    glm::vec3 cap_lp = world_to_local(tip_center, frame);
                    uint32_t cap_idx = add_vert(tip_center, frame.z, cap_lp,
                                                glm::vec3(0.0f, 0.0f, 1.0f), bi, 0.0f);
                    fan_cap(foot_ring, sides, cap_idx, false);
                } else {
                    // Hand: simple centroid fan.
                    glm::vec3 cap_lp = world_to_local(pb, frame);
                    glm::vec3 cap_ln = glm::vec3(0.0f, 0.0f, 1.0f);
                    uint32_t cap_idx = add_vert(pb, frame.z, cap_lp, cap_ln, bi, 0.0f);
                    fan_cap(ring1, sides, cap_idx, false);
                }
            }
        }
    }

    // Root cap (start of bone 0).
    if (bone0_ring0_set) {
        const glm::vec3 &pa = bind_pose.joints[BONES[0].a];
        const glm::vec3 &pb = bind_pose.joints[BONES[0].b];
        BoneFrame frame = make_frame(pa, pb);
        glm::vec3 root_lp = world_to_local(pa, frame);
        glm::vec3 root_ln = glm::vec3(0.0f, 0.0f, -1.0f);
        uint32_t root_idx = add_vert(pa, -frame.z, root_lp, root_ln, 0, 1.0f);
        fan_cap(bone0_ring0, std::max(3, profiles[0].sides), root_idx, true);
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// deform_skeleton_mesh — CPU LBS
// Transforms each vertex from its bone-local rest space to world space using
// the current joint transform. Applies inverse-transpose for normals.
// ---------------------------------------------------------------------------
void deform_skeleton_mesh(SkeletonMesh &mesh, const SkeletonPose &pose) {
    const size_t n = mesh.vertices.size();
    const bool has_normals = (mesh.rest_normals.size() == n);

    for (size_t i = 0; i < n; ++i) {
        SkeletonVertex &v = mesh.vertices[i];
        int bi = std::max(0, std::min((int)v.bone_index0, NUM_BONES - 1));

        int ja = BONES[bi].a;
        int jb = BONES[bi].b;

        BoneFrame f = make_frame(pose.joints[ja], pose.joints[jb]);

        // Transform rest-pose local position to world.
        v.position = local_to_world(mesh.rest_positions[i], f);

        // Transform rest-pose local normal to world via the frame's rotation.
        // For orthonormal frames the inverse-transpose equals the forward transform.
        if (has_normals) {
            v.normal = glm::normalize(local_dir_to_world(mesh.rest_normals[i], f));
        }
    }
}

// ---------------------------------------------------------------------------
// Overload: vector<BoneProfile> → convert to BoneProfileArray then delegate.
// ---------------------------------------------------------------------------
SkeletonMesh generate_skeleton_mesh(const Skeleton                 &skel,
                                    const std::vector<BoneProfile> &profiles) {
    BoneProfileArray arr;
    for (int i = 0; i < NUM_BONE_PROFILES && i < (int)profiles.size(); ++i)
        arr[(size_t)i] = profiles[(size_t)i];
    return generate_skeleton_mesh(skel, arr);
}
