#include "rig_animation.h"
#include "rig.h"
#include "animation_log.h"
#include "input/input.h"
#include "camera/camera.h"
#include "terrain/map_util.h"
#include "game_state.h"
#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

// File-scope animation config — single instance shared by GaitSystem and RigRenderer.
static const AnimationConfig s_anim_cfg{};

// Draw a 3-sided triangular prism (wireframe strut) between two points.
// Produces 18 vertices (3 quad faces, each split into 2 triangles).
static void emit_strut(std::vector<BasaltVertex> &verts,
                       const glm::vec3 &pA, const glm::vec3 &pB,
                       float thickness) {
    glm::vec3 seg = pB - pA;
    float len = glm::length(seg);
    if (len < 1e-5f) return;
    glm::vec3 dir = seg / len;

    // Stable perpendicular basis — fallback when dir is near-parallel to Z.
    glm::vec3 right;
    if (fabsf(glm::dot(dir, glm::vec3(0.0f, 0.0f, 1.0f))) < 0.99f)
        right = glm::normalize(glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), dir));
    else
        right = glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), dir));
    glm::vec3 fwd = glm::cross(dir, right);

    // 3-sided ring offsets at 0°, 120°, 240°.
    constexpr int SIDES = 3;
    constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    glm::vec3 ring_A[SIDES], ring_B[SIDES];
    for (int i = 0; i < SIDES; ++i) {
        float angle = (float)i * (TWO_PI / (float)SIDES);
        glm::vec3 offset = (right * cosf(angle) + fwd * sinf(angle)) * thickness;
        ring_A[i] = pA + offset;
        ring_B[i] = pB + offset;
    }

    // Hardcoded neon cyan with sheen 0.3.
    constexpr float cr = 0.0f, cg = 1.0f, cb = 1.0f;
    constexpr float sheen = 0.3f;

    auto push = [&](const glm::vec3 &pos, const glm::vec3 &n) {
        BasaltVertex bv;
        bv.pos_x   = pos.x;  bv.pos_y   = pos.y;  bv.pos_z   = pos.z;
        bv.color_r = cr;      bv.color_g = cg;      bv.color_b = cb;
        bv.sheen   = sheen;
        bv.nx = n.x; bv.ny = n.y; bv.nz = n.z;
        verts.push_back(bv);
    };

    // Emit 3 quad faces (6 triangles, 18 vertices).
    for (int i = 0; i < SIDES; ++i) {
        int j = (i + 1) % SIDES;

        glm::vec3 p00 = ring_A[i];
        glm::vec3 p10 = ring_A[j];
        glm::vec3 p01 = ring_B[i];
        glm::vec3 p11 = ring_B[j];

        // Flat-shaded face normal.
        glm::vec3 cross_val = glm::cross(p10 - p00, p01 - p00);
        float cross_len = glm::length(cross_val);
        glm::vec3 face_n = (cross_len > 1e-7f) ? (cross_val / cross_len) : dir;

        // Two triangles per quad.
        push(p00, face_n); push(p10, face_n); push(p01, face_n);
        push(p10, face_n); push(p11, face_n); push(p01, face_n);
    }
}

// Emit wireframe struts forming a tapered cage along a bone.
// mat_A: start joint transform (col0=Right, col1=Fwd, col2=Up, col3=Pos).
// pos_B: end joint world position.
// radius_A/radius_B: cross-section radii at start/end (allows tapering).
// segments: 3 or 4 for low-poly net aesthetic.
// color: xyz=RGB, w=sheen.
static void append_bone_cage(std::vector<BasaltVertex> &verts,
                              const glm::mat4 &mat_A,
                              const glm::vec3 &pos_B,
                              float radius_A, float radius_B,
                              int segments, glm::vec4 color) {
    glm::vec3 pos_A      = glm::vec3(mat_A[3]);
    glm::vec3 bone_right = glm::vec3(mat_A[0]);
    glm::vec3 bone_fwd   = glm::vec3(mat_A[1]);

    glm::vec3 bone_vec = pos_B - pos_A;
    float bone_len = glm::length(bone_vec);
    if (bone_len < 1e-5f) return;
    (void)color;

    segments = std::max(segments, 3);
    segments = std::min(segments, 8);

    constexpr int MAX_SIDES = 8;
    constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    glm::vec3 ring_A[MAX_SIDES];
    glm::vec3 ring_B[MAX_SIDES];

    for (int i = 0; i < segments; ++i) {
        float angle = (float)i * (TWO_PI / (float)segments);
        float ca = cosf(angle), sa = sinf(angle);
        glm::vec3 dir = bone_right * ca + bone_fwd * sa;

        ring_A[i] = pos_A + dir * radius_A;
        ring_B[i] = pos_B + dir * radius_B;
    }

    constexpr float STRUT_THICKNESS = 0.003f;