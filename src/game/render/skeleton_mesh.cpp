#include "skeleton_mesh.h"
#include "actor.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Bone table: (start_joint, end_joint) pairs that define each bone segment.
// ---------------------------------------------------------------------------
static constexpr struct { int a, b; } BONES[] = {
    { (int)Joint::ROOT,       (int)Joint::SPINE      },
    { (int)Joint::SPINE,      (int)Joint::CHEST      },
    { (int)Joint::CHEST,      (int)Joint::NECK       },
    { (int)Joint::NECK,       (int)Joint::HEAD       },
    { (int)Joint::CHEST,      (int)Joint::L_SHOULDER },
    { (int)Joint::L_SHOULDER, (int)Joint::L_ELBOW    },
    { (int)Joint::L_ELBOW,    (int)Joint::L_WRIST    },
    { (int)Joint::CHEST,      (int)Joint::R_SHOULDER },
    { (int)Joint::R_SHOULDER, (int)Joint::R_ELBOW    },
    { (int)Joint::R_ELBOW,    (int)Joint::R_WRIST    },
    { (int)Joint::SPINE,      (int)Joint::L_HIP      },
    { (int)Joint::L_HIP,      (int)Joint::L_KNEE     },
    { (int)Joint::L_KNEE,     (int)Joint::L_ANKLE    },
    { (int)Joint::SPINE,      (int)Joint::R_HIP      },
    { (int)Joint::R_HIP,      (int)Joint::R_KNEE     },
    { (int)Joint::R_KNEE,     (int)Joint::R_ANKLE    },
};
static constexpr int NUM_BONES = (int)(sizeof(BONES) / sizeof(BONES[0]));

// Joints that get an end cap (tip of each extremity).
static constexpr int TERMINAL_JOINTS[] = {
    (int)Joint::HEAD,
    (int)Joint::L_WRIST,
    (int)Joint::R_WRIST,
    (int)Joint::L_ANKLE,
    (int)Joint::R_ANKLE,
};

// ---------------------------------------------------------------------------
// Bone local frame.
// origin = joints[a].  Z = toward joints[b].  X, Y perpendicular.
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
SkeletonMesh generate_skeleton_mesh(const SkeletonPose &bind_pose,
                                    const BoneProfiles  &profiles) {
    SkeletonMesh mesh;
    mesh.vertices.reserve(256);
    mesh.indices.reserve(512);
    mesh.rest_positions.reserve(256);

    // Helper: add a vertex, return its index.
    auto add_vert = [&](const glm::vec3 &world_pos,
                        const glm::vec3 &normal,
                        const glm::vec3 &local_pos,
                        int              bone_a,
                        int              bone_b,
                        float            weight) -> uint32_t {
        uint32_t idx = (uint32_t)mesh.vertices.size();
        SkeletonVertex sv;
        sv.position     = world_pos;
        sv.normal       = normal;
        sv.bone_indices = { bone_a, bone_b };
        sv.bone_weight  = weight;
        mesh.vertices.push_back(sv);
        mesh.rest_positions.push_back(local_pos);
        return idx;
    };

    // For each bone: extrude a tapered cylinder.
    for (int bi = 0; bi < NUM_BONES; ++bi) {
        int ja = BONES[bi].a;
        int jb = BONES[bi].b;

        const BoneProfile &prof = profiles[(size_t)ja];
        int   sides = std::max(3, prof.sides);
        float r0    = prof.radius;
        float r1    = prof.radius * (1.0f - prof.taper);

        const glm::vec3 &pa = bind_pose.joints[ja];
        const glm::vec3 &pb = bind_pose.joints[jb];

        BoneFrame frame = make_frame(pa, pb);

        // Emit start ring (weight=1 → fully bone ja) and end ring (weight=0 → bone jb).
        // base_start and base_end are the offsets into the vertex array.
        uint32_t base_start = (uint32_t)mesh.vertices.size();

        for (int s = 0; s < sides; ++s) {
            float angle = s * glm::two_pi<float>() / sides;
            float cx    = cosf(angle);
            float cy    = sinf(angle);

            // Start ring vertex (z=0 in local frame).
            glm::vec3 offset_start = frame.x * (cx * r0) + frame.y * (cy * r0);
            glm::vec3 wp_start     = pa + offset_start;
            glm::vec3 lp_start     = world_to_local(wp_start, frame);
            glm::vec3 n_start      = glm::normalize(offset_start);
            add_vert(wp_start, n_start, lp_start, ja, jb, 1.0f);
        }

        uint32_t base_end = (uint32_t)mesh.vertices.size();

        for (int s = 0; s < sides; ++s) {
            float angle = s * glm::two_pi<float>() / sides;
            float cx    = cosf(angle);
            float cy    = sinf(angle);

            // End ring vertex (z=frame.length in local frame).
            glm::vec3 offset_end = frame.x * (cx * r1) + frame.y * (cy * r1);
            glm::vec3 wp_end     = pb + offset_end;
            glm::vec3 lp_end     = world_to_local(wp_end, frame);
            glm::vec3 n_end      = (r1 > 1e-5f)
                                 ? glm::normalize(offset_end)
                                 : frame.z;
            add_vert(wp_end, n_end, lp_end, ja, jb, 0.0f);
        }

        // Connect rings with quads (2 triangles each).
        for (int s = 0; s < sides; ++s) {
            int sn = (s + 1) % sides;
            uint32_t v00 = base_start + (uint32_t)s;
            uint32_t v10 = base_start + (uint32_t)sn;
            uint32_t v01 = base_end   + (uint32_t)s;
            uint32_t v11 = base_end   + (uint32_t)sn;

            mesh.indices.push_back(v00);
            mesh.indices.push_back(v10);
            mesh.indices.push_back(v01);

            mesh.indices.push_back(v10);
            mesh.indices.push_back(v11);
            mesh.indices.push_back(v01);
        }
    }

    // End caps: fan from centroid for terminal joints.
    // Find which bone ends at each terminal joint.
    for (int tj : TERMINAL_JOINTS) {
        // Locate the bone that ends at this terminal joint.
        int bone_a = -1, bone_b = -1;
        for (int bi = 0; bi < NUM_BONES; ++bi) {
            if (BONES[bi].b == tj) {
                bone_a = BONES[bi].a;
                bone_b = BONES[bi].b;
                break;
            }
        }
        if (bone_a < 0) continue;

        const BoneProfile &prof  = profiles[(size_t)bone_a];
        int   sides = std::max(3, prof.sides);
        float r1    = prof.radius * (1.0f - prof.taper);

        const glm::vec3 &pa  = bind_pose.joints[bone_a];
        const glm::vec3 &pb  = bind_pose.joints[bone_b];
        BoneFrame frame = make_frame(pa, pb);

        // Cap centroid (at the tip joint, local z = frame.length).
        glm::vec3 lp_center = world_to_local(pb, frame);
        uint32_t center_idx = add_vert(pb, frame.z, lp_center, bone_a, bone_b, 0.0f);

        // Perimeter ring (same as end ring of the bone above but re-emitted for caps).
        uint32_t ring_base = (uint32_t)mesh.vertices.size();
        for (int s = 0; s < sides; ++s) {
            float angle = s * glm::two_pi<float>() / sides;
            float cx    = cosf(angle);
            float cy    = sinf(angle);

            glm::vec3 offset = frame.x * (cx * r1) + frame.y * (cy * r1);
            glm::vec3 wp     = pb + offset;
            glm::vec3 lp     = world_to_local(wp, frame);
            add_vert(wp, frame.z, lp, bone_a, bone_b, 0.0f);
        }

        // Fan triangles (winding: cap faces outward along +Z of bone).
        for (int s = 0; s < sides; ++s) {
            int sn = (s + 1) % sides;
            mesh.indices.push_back(center_idx);
            mesh.indices.push_back(ring_base + (uint32_t)sn);
            mesh.indices.push_back(ring_base + (uint32_t)s);
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

        glm::vec3 edge0 = p1 - p0;
        glm::vec3 edge1 = p2 - p0;
        glm::vec3 face_n = glm::cross(edge0, edge1); // magnitude = 2 * area

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
// deform_skeleton_mesh — LBS update
// ---------------------------------------------------------------------------
void deform_skeleton_mesh(SkeletonMesh &mesh, const SkeletonPose &pose) {
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        SkeletonVertex &v  = mesh.vertices[i];
        const glm::vec3 &lp = mesh.rest_positions[i];

        int   ja = v.bone_indices[0];
        int   jb = v.bone_indices[1];
        float w  = v.bone_weight;

        // Primary bone transform (ja → jb).
        BoneFrame f0 = make_frame(pose.joints[ja], pose.joints[jb]);
        glm::vec3 wp0 = local_to_world(lp, f0);

        if (w >= 1.0f - 1e-4f) {
            // Single bone influence — common case.
            v.position = wp0;
        } else {
            // Secondary bone transform (jb → ja, reversed for blend).
            BoneFrame f1 = make_frame(pose.joints[jb], pose.joints[ja]);
            // Express rest position relative to jb end in reversed frame.
            glm::vec3 lp1 = { lp.x, lp.y, f0.length - lp.z };
            glm::vec3 wp1 = local_to_world(lp1, f1);
            v.position    = glm::mix(wp1, wp0, w);
        }
    }
}
