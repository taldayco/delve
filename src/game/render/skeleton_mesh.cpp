#include "skeleton_mesh.h"
#include "actor.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
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

// Joints that get an end cap (extremities: head, hands, feet).
static constexpr int TERMINAL_JOINTS[] = {
    (int)Joint::HEAD,
    (int)Joint::L_WRIST,
    (int)Joint::R_WRIST,
    (int)Joint::L_ANKLE,
    (int)Joint::R_ANKLE,
};

// ---------------------------------------------------------------------------
// Local bone frame.
// origin = joints[a].  z = toward joints[b].  x, y perpendicular.
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

// Project world_pos into local bone frame.
static glm::vec3 world_to_local(const glm::vec3 &world_pos, const BoneFrame &f) {
    glm::vec3 d = world_pos - f.origin;
    return { glm::dot(d, f.x), glm::dot(d, f.y), glm::dot(d, f.z) };
}

// Reconstruct world position from local bone-space coords.
static glm::vec3 local_to_world(const glm::vec3 &local, const BoneFrame &f) {
    return f.origin + f.x * local.x + f.y * local.y + f.z * local.z;
}

// ---------------------------------------------------------------------------
// generate_skeleton_mesh
// ---------------------------------------------------------------------------
SkeletonMesh generate_skeleton_mesh(const SkeletonPose     &bind_pose,
                                    const BoneProfileArray &profiles) {
    SkeletonMesh mesh;
    mesh.vertices.reserve(256);
    mesh.indices.reserve(512);
    mesh.rest_positions.reserve(256);

    static constexpr float PI = 3.14159265358979323846f;

    // Helper: add a vertex, return its index.
    auto add_vert = [&](const glm::vec3 &world_pos,
                        const glm::vec3 &normal,
                        const glm::vec3 &local_pos,
                        int              bi,   // bone index for primary influence
                        float            weight) -> uint32_t {
        uint32_t idx = (uint32_t)mesh.vertices.size();
        SkeletonVertex sv;
        sv.position    = world_pos;
        sv.normal      = normal;
        sv.bone_index0 = (float)bi;
        sv.bone_weight = weight;
        sv.bone_index1 = (float)bi; // single-bone verts: secondary == primary
        mesh.vertices.push_back(sv);
        mesh.rest_positions.push_back(local_pos);
        return idx;
    };

    // For each bone: extrude a tapered cylinder.
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

        // Apply twist to the frame's x/y axes.
        if (fabsf(twist) > 1e-5f) {
            float c = cosf(twist), s = sinf(twist);
            glm::vec3 nx = frame.x * c + frame.y * s;
            glm::vec3 ny = -frame.x * s + frame.y * c;
            frame.x = nx;
            frame.y = ny;
        }

        uint32_t base_start = (uint32_t)mesh.vertices.size();

        // Start ring (weight=1 → fully bone bi).
        for (int si = 0; si < sides; ++si) {
            float angle = si * (2.0f * PI / sides);
            glm::vec3 offset = frame.x * (cosf(angle) * r0) + frame.y * (sinf(angle) * r0);
            glm::vec3 wp = pa + offset;
            glm::vec3 lp = world_to_local(wp, frame);
            glm::vec3 n  = glm::normalize(offset);
            add_vert(wp, n, lp, bi, 1.0f);
        }

        uint32_t base_end = (uint32_t)mesh.vertices.size();

        // End ring (weight=0 → bone bi, same bone but representing child-joint influence).
        for (int si = 0; si < sides; ++si) {
            float angle = si * (2.0f * PI / sides);
            glm::vec3 offset = frame.x * (cosf(angle) * r1) + frame.y * (sinf(angle) * r1);
            glm::vec3 wp = pb + offset;
            glm::vec3 lp = world_to_local(wp, frame);
            glm::vec3 n  = (r1 > 1e-5f) ? glm::normalize(offset) : frame.z;
            add_vert(wp, n, lp, bi, 0.0f);
        }

        // Connect rings with quads (2 triangles each).
        for (int si = 0; si < sides; ++si) {
            int sn = (si + 1) % sides;
            uint32_t v00 = base_start + (uint32_t)si;
            uint32_t v10 = base_start + (uint32_t)sn;
            uint32_t v01 = base_end   + (uint32_t)si;
            uint32_t v11 = base_end   + (uint32_t)sn;

            mesh.indices.push_back(v00); mesh.indices.push_back(v10); mesh.indices.push_back(v01);
            mesh.indices.push_back(v10); mesh.indices.push_back(v11); mesh.indices.push_back(v01);
        }
    }

    // End caps for terminal joints.
    for (int tj : TERMINAL_JOINTS) {
        // Find the bone that ends at this joint.
        int bone_idx = -1;
        for (int bi = 0; bi < NUM_BONES; ++bi) {
            if (BONES[bi].b == tj) { bone_idx = bi; break; }
        }
        if (bone_idx < 0) continue;

        int bone_a = BONES[bone_idx].a;
        int bone_b = BONES[bone_idx].b;

        const BoneProfile &prof = profiles[(size_t)bone_idx];
        int   sides = std::max(3, prof.sides);
        float r1    = prof.radius_end;

        BoneFrame frame = make_frame(bind_pose.joints[bone_a], bind_pose.joints[bone_b]);

        const glm::vec3 &pb = bind_pose.joints[bone_b];

        glm::vec3 lp_center = world_to_local(pb, frame);
        uint32_t center_idx = add_vert(pb, frame.z, lp_center, bone_idx, 0.0f);

        uint32_t ring_base = (uint32_t)mesh.vertices.size();
        for (int si = 0; si < sides; ++si) {
            float angle = si * (2.0f * glm::pi<float>() / sides);
            glm::vec3 offset = frame.x * (cosf(angle) * r1) + frame.y * (sinf(angle) * r1);
            glm::vec3 wp = pb + offset;
            glm::vec3 lp = world_to_local(wp, frame);
            add_vert(wp, frame.z, lp, bone_idx, 0.0f);
        }

        for (int si = 0; si < sides; ++si) {
            int sn = (si + 1) % sides;
            mesh.indices.push_back(center_idx);
            mesh.indices.push_back(ring_base + (uint32_t)sn);
            mesh.indices.push_back(ring_base + (uint32_t)si);
        }
    }

    // Smooth normals: accumulate area-weighted face normals.
    std::vector<glm::vec3> accum(mesh.vertices.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];

        const glm::vec3 &p0 = mesh.vertices[i0].position;
        const glm::vec3 &p1 = mesh.vertices[i1].position;
        const glm::vec3 &p2 = mesh.vertices[i2].position;

        glm::vec3 face_n = glm::cross(p1 - p0, p2 - p0);
        accum[i0] += face_n;
        accum[i1] += face_n;
        accum[i2] += face_n;
    }
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        float len = glm::length(accum[i]);
        if (len > 1e-6f)
            mesh.vertices[i].normal = accum[i] / len;
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// deform_skeleton_mesh — CPU LBS update
// Writes world-space positions/normals into mesh.vertices.
// The vertex shader performs no additional bone transform.
// ---------------------------------------------------------------------------
void deform_skeleton_mesh(SkeletonMesh &mesh, const SkeletonPose &pose) {
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        SkeletonVertex &v  = mesh.vertices[i];
        const glm::vec3 &lp = mesh.rest_positions[i];

        int   bi = (int)v.bone_index0;
        float w  = v.bone_weight;

        // Clamp bone index to valid range.
        bi = std::max(0, std::min(bi, NUM_BONES - 1));

        int ja = BONES[bi].a;
        int jb = BONES[bi].b;

        BoneFrame f0 = make_frame(pose.joints[ja], pose.joints[jb]);
        glm::vec3 wp0 = local_to_world(lp, f0);

        if (w >= 1.0f - 1e-4f) {
            v.position = wp0;
        } else {
            // Blend with reversed frame for joint-straddling vertices.
            BoneFrame f1 = make_frame(pose.joints[jb], pose.joints[ja]);
            glm::vec3 lp1 = { lp.x, lp.y, f0.length - lp.z };
            glm::vec3 wp1 = local_to_world(lp1, f1);
            v.position    = glm::mix(wp1, wp0, w);
        }
    }

    // Recompute smooth normals from deformed positions.
    std::vector<glm::vec3> accum(mesh.vertices.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];
        const glm::vec3 &p0 = mesh.vertices[i0].position;
        const glm::vec3 &p1 = mesh.vertices[i1].position;
        const glm::vec3 &p2 = mesh.vertices[i2].position;
        glm::vec3 face_n = glm::cross(p1 - p0, p2 - p0);
        accum[i0] += face_n;
        accum[i1] += face_n;
        accum[i2] += face_n;
    }
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        float len = glm::length(accum[i]);
        if (len > 1e-6f)
            mesh.vertices[i].normal = accum[i] / len;
    }
}
