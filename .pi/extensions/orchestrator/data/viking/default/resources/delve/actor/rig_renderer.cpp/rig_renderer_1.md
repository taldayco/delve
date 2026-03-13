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