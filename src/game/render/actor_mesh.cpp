#include "render/actor_mesh.h"
#include "actor.h"
#include "render/skeleton_mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

// Bone table — must match BONES in skeleton_mesh.cpp and BoneSeg in actor_renderer.h.
static constexpr struct { int a, b; } WIRE_BONES[] = {
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
static constexpr int NUM_WIRE_BONES = (int)(sizeof(WIRE_BONES) / sizeof(WIRE_BONES[0]));
static_assert(NUM_WIRE_BONES == NUM_BONE_PROFILES, "WIRE_BONES table must match NUM_BONE_PROFILES");

// Build a local bone frame (origin, x, y, z axes).
struct WireFrame {
    glm::vec3 origin, x, y, z;
};

static WireFrame make_wire_frame(const glm::vec3 &a, const glm::vec3 &b) {
    WireFrame f;
    f.origin = a;
    f.z = b - a;
    float len = glm::length(f.z);
    f.z = (len > 1e-5f) ? f.z / len : glm::vec3(0.f, 0.f, 1.f);
    glm::vec3 up(0.f, 0.f, 1.f);
    if (fabsf(glm::dot(f.z, up)) > 0.99f) up = glm::vec3(1.f, 0.f, 0.f);
    f.x = glm::normalize(glm::cross(f.z, up));
    f.y = glm::cross(f.x, f.z);
    return f;
}

// Emit all line-list edges for one bone into `out`.
// Each bone segment uses its BoneProfile color for visual distinction.
// Edges: start ring (N), end ring (N), longitudinal (N) = 3N line pairs = 6N vertices.
static void emit_bone_wireframe(const glm::vec3 &pa, const glm::vec3 &pb,
                                const BoneProfile &prof,
                                std::vector<ActorMeshVertex> &out) {
    const glm::vec4 bone_color{1.f, 1.f, 1.f, 1.f};
    static constexpr float PI = 3.14159265358979323846f;

    int sides = std::max(3, prof.sides);
    float r0 = prof.radius_start;
    float r1 = prof.radius_end;
    float twist = prof.twist * (PI / 180.f);

    WireFrame frame = make_wire_frame(pa, pb);
    if (fabsf(twist) > 1e-5f) {
        float c = cosf(twist), s = sinf(twist);
        glm::vec3 nx = frame.x * c + frame.y * s;
        glm::vec3 ny = -frame.x * s + frame.y * c;
        frame.x = nx;
        frame.y = ny;
    }

    // Pre-compute ring vertex positions.
    std::vector<glm::vec3> ring0(sides), ring1(sides);
    for (int si = 0; si < sides; ++si) {
        float angle = si * (2.f * PI / sides);
        float ca = cosf(angle), sa = sinf(angle);
        ring0[si] = pa + frame.x * (ca * r0) + frame.y * (sa * r0);
        ring1[si] = pb + frame.x * (ca * r1) + frame.y * (sa * r1);
    }

    // Start ring edges.
    for (int si = 0; si < sides; ++si) {
        int next = (si + 1) % sides;
        out.push_back({ring0[si],   bone_color});
        out.push_back({ring0[next], bone_color});
    }

    // End ring edges.
    for (int si = 0; si < sides; ++si) {
        int next = (si + 1) % sides;
        out.push_back({ring1[si],   bone_color});
        out.push_back({ring1[next], bone_color});
    }

    // Longitudinal edges connecting start ring to end ring.
    for (int si = 0; si < sides; ++si) {
        out.push_back({ring0[si], bone_color});
        out.push_back({ring1[si], bone_color});
    }
}

ActorMesh generate_actor_wireframe_mesh(const SkeletonPose     &pose,
                                        const BoneProfileArray &profiles) {
    ActorMesh mesh;
    // 3N line pairs per bone, 2 vertices per pair. Pre-reserve assuming avg sides=5.
    mesh.vertices.reserve(NUM_WIRE_BONES * 3 * 5 * 2);

    for (int bi = 0; bi < NUM_WIRE_BONES; ++bi) {
        const glm::vec3 &pa = pose.joints[WIRE_BONES[bi].a];
        const glm::vec3 &pb = pose.joints[WIRE_BONES[bi].b];
        emit_bone_wireframe(pa, pb, profiles[(size_t)bi], mesh.vertices);
    }

    return mesh;
}

void deform_actor_mesh(ActorMesh &mesh, const SkeletonPose &pose,
                       const BoneProfileArray &profiles) {
    // Rebuild from scratch — the vertex count is small (< 1000) so this is fast.
    // This also picks up any BoneProfile parameter changes (hot-reload).
    mesh.vertices.clear();
    for (int bi = 0; bi < NUM_WIRE_BONES; ++bi) {
        const glm::vec3 &pa = pose.joints[WIRE_BONES[bi].a];
        const glm::vec3 &pb = pose.joints[WIRE_BONES[bi].b];
        emit_bone_wireframe(pa, pb, profiles[(size_t)bi], mesh.vertices);
    }
}