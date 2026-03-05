#include "render/actor_renderer.h"
#include "actor.h"
#include "config.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>
#include <algorithm>

static constexpr float PI = 3.14159265358979323846f;

ActorProportions ActorRenderer::make_proportions(float total_height_feet) {
    // Classical 8-head canon:
    //   head=1/8, neck=1/8, torso=2/8 (chest+abdomen), pelvis=1/8
    //   upper_leg=2/8, lower_leg=1.5/8, foot=0.5/8
    //   shoulder_width=2/8, hip_width=1.5/8, waist=1.25/8
    //   upper_arm=1.5/8, forearm=1.25/8, hand=0.75/8
    float h = total_height_feet;
    float u = h / 8.0f; // one head-unit

    ActorProportions p;
    p.total_height   = h;
    p.head_height    = 1.0f  * u;
    p.neck_height    = 0.5f  * u;
    p.torso_height   = 2.0f  * u;
    p.upper_leg      = 2.0f  * u;
    p.lower_leg      = 2.0f  * u;  // feet at y=0, ankle, knee, crotch at y=4u = total_height/2
    p.shoulder_width = 3.0f  * u;
    p.hip_width      = 1.5f  * u;
    p.waist_width    = 1.25f * u;
    p.head_width     = 0.9f  * u;
    p.upper_arm      = 1.5f  * u;
    p.forearm        = 1.25f * u;
    p.hand_length    = 0.75f * u;
    p.foot_length    = 1.0f  * u;
    return p;
}

std::vector<SkeletonJoint> ActorRenderer::build_skeleton(const ActorProportions &p) {
    std::vector<SkeletonJoint> joints;
    joints.reserve(16);

    float u = p.total_height / 8.0f;

    // Heights (y-up in local space, origin at feet)
    float knee_y    = p.lower_leg;
    float crotch_y  = p.lower_leg + p.upper_leg;          // == total_height/2
    float chest_y   = crotch_y + p.torso_height;
    float shoulder_y = chest_y;
    float neck_y    = chest_y + 0.5f * u;
    float head_base_y = neck_y + p.neck_height;
    float head_center_y = head_base_y + p.head_height * 0.5f;

    float hw = p.hip_width * 0.5f;
    float sw = p.shoulder_width * 0.5f;

    float limb_r  = u * 0.18f;
    float torso_r = u * 0.30f;

    // Head
    joints.push_back({ {0, head_center_y, 0}, {p.head_width*0.5f, p.head_width*0.45f, p.head_height*0.5f}, "head" });
    // Neck
    joints.push_back({ {0, neck_y, 0}, {limb_r, limb_r, p.neck_height*0.5f}, "neck" });
    // Torso (center at midpoint between crotch and shoulders)
    joints.push_back({ {0, crotch_y + p.torso_height * 0.5f, 0}, {p.shoulder_width*0.5f, torso_r, p.torso_height*0.5f}, "torso" });
    // Pelvis / crotch
    joints.push_back({ {0, crotch_y, 0}, {p.hip_width*0.5f, torso_r, u*0.5f}, "pelvis" });

    // Left upper leg
    joints.push_back({ {-hw, knee_y + p.upper_leg*0.5f, 0}, {limb_r, limb_r, p.upper_leg*0.5f}, "l_upper_leg" });
    // Left lower leg
    joints.push_back({ {-hw, p.lower_leg*0.5f, 0}, {limb_r*0.85f, limb_r*0.85f, p.lower_leg*0.5f}, "l_lower_leg" });
    // Right upper leg
    joints.push_back({ { hw, knee_y + p.upper_leg*0.5f, 0}, {limb_r, limb_r, p.upper_leg*0.5f}, "r_upper_leg" });
    // Right lower leg
    joints.push_back({ { hw, p.lower_leg*0.5f, 0}, {limb_r*0.85f, limb_r*0.85f, p.lower_leg*0.5f}, "r_lower_leg" });

    // Left upper arm
    joints.push_back({ {-(sw + p.upper_arm*0.5f), shoulder_y, 0}, {p.upper_arm*0.5f, limb_r, limb_r}, "l_upper_arm" });
    // Left forearm
    joints.push_back({ {-(sw + p.upper_arm + p.forearm*0.5f), shoulder_y, 0}, {p.forearm*0.5f, limb_r*0.8f, limb_r*0.8f}, "l_forearm" });
    // Right upper arm
    joints.push_back({ { (sw + p.upper_arm*0.5f), shoulder_y, 0}, {p.upper_arm*0.5f, limb_r, limb_r}, "r_upper_arm" });
    // Right forearm
    joints.push_back({ { (sw + p.upper_arm + p.forearm*0.5f), shoulder_y, 0}, {p.forearm*0.5f, limb_r*0.8f, limb_r*0.8f}, "r_forearm" });

    return joints;
}

void ActorRenderer::init(SDL_GPUDevice *device,
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
    bi.size  = MAX_ACTOR_VERTICES * sizeof(BasaltVertex);
    actor_vbo = SDL_CreateGPUBuffer(device, &bi);
    if (!actor_vbo) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ActorRenderer: Failed to create vertex buffer: %s", SDL_GetError());
        return;
    }

    // Persistent transfer buffer for zero-alloc uploads.
    SDL_GPUTransferBufferCreateInfo tbi = {};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size  = MAX_ACTOR_VERTICES * sizeof(BasaltVertex);
    transfer_buf = SDL_CreateGPUTransferBuffer(device, &tbi);
    if (!transfer_buf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ActorRenderer: Failed to create transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUBuffer(device, actor_vbo);
        actor_vbo = nullptr;
        return;
    }

    initialized = true;
    SDL_Log("ActorRenderer: Initialized (VBO capacity: %u vertices)", MAX_ACTOR_VERTICES);
}

void ActorRenderer::cleanup(SDL_GPUDevice *device) {
    if (actor_vbo)    { SDL_ReleaseGPUBuffer(device, actor_vbo);           actor_vbo    = nullptr; }
    if (transfer_buf) { SDL_ReleaseGPUTransferBuffer(device, transfer_buf); transfer_buf = nullptr; }
    // pipeline and dummy_ssbo_ are borrowed — not released here.
    pipeline    = nullptr;
    dummy_ssbo_ = nullptr;
    initialized = false;
}

void ActorRenderer::emit_cylinder(const glm::vec3 &a, const glm::vec3 &b,
                                   float radius, glm::vec3 color, int sides,
                                   std::vector<BasaltVertex> &out_verts) {
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

uint32_t ActorRenderer::prepare(SDL_GPUCommandBuffer *cmd, flecs::world &ecs) {
    if (!initialized || !actor_vbo || !transfer_buf) return 0;

    std::vector<BasaltVertex> verts;
    verts.reserve(512);

    ActorProportions props = make_proportions(Config::HUMAN_HEIGHT_FEET);
    float u       = props.total_height / 8.0f;
    float inv_wu  = 1.0f / Config::WORLD_UNIT;
    float limb_r  = u * 0.18f * inv_wu;
    float torso_r = u * 0.30f * inv_wu;
    float head_r  = props.head_width * 0.5f * inv_wu;
    float head_hh = props.head_height * 0.5f * inv_wu;

    glm::vec3 skin_color(0.72f, 0.60f, 0.50f);
    glm::vec3 cloth_color(0.35f, 0.45f, 0.55f);

    // Convert a local-space joint position (feet, Y-up, origin at feet) to world space.
    auto to_world = [inv_wu](const glm::vec3 &local, const Transform &t) -> glm::vec3 {
        float cos_f = cosf(t.facing), sin_f = sinf(t.facing);
        float lx = local.x * inv_wu;
        float lz = local.z * inv_wu;
        float ly = local.y * inv_wu; // height → wz
        return { t.x + cos_f * lx - sin_f * lz,
                 t.y + sin_f * lx + cos_f * lz,
                 t.z + ly };
    };

    ecs.each([&](ActorTag, const Transform &t, const SkeletonPose &pose, const ActorConfig &) {
        auto jp = [&](Joint j) { return to_world(pose.joints[(int)j], t); };

        // Head (vertical capsule around HEAD joint)
        glm::vec3 head_pos = jp(Joint::HEAD);
        emit_cylinder(head_pos - glm::vec3(0.f, 0.f, head_hh),
                      head_pos + glm::vec3(0.f, 0.f, head_hh),
                      head_r, skin_color, 8, verts);

        // Neck
        emit_cylinder(jp(Joint::NECK), jp(Joint::HEAD), limb_r, skin_color, 6, verts);

        // Torso: SPINE to CHEST
        emit_cylinder(jp(Joint::SPINE), jp(Joint::CHEST), torso_r, cloth_color, 8, verts);

        // Pelvis: L_HIP to R_HIP (horizontal crossbar)
        emit_cylinder(jp(Joint::L_HIP), jp(Joint::R_HIP), limb_r * 1.2f, cloth_color, 6, verts);

        // Arms
        emit_cylinder(jp(Joint::L_SHOULDER), jp(Joint::L_ELBOW), limb_r,        skin_color, 6, verts);
        emit_cylinder(jp(Joint::L_ELBOW),    jp(Joint::L_WRIST),  limb_r * 0.8f, skin_color, 6, verts);
        emit_cylinder(jp(Joint::R_SHOULDER), jp(Joint::R_ELBOW), limb_r,        skin_color, 6, verts);
        emit_cylinder(jp(Joint::R_ELBOW),    jp(Joint::R_WRIST),  limb_r * 0.8f, skin_color, 6, verts);

        // Legs
        emit_cylinder(jp(Joint::L_HIP),  jp(Joint::L_KNEE),  limb_r,        cloth_color, 6, verts);
        emit_cylinder(jp(Joint::L_KNEE), jp(Joint::L_ANKLE), limb_r * 0.85f, cloth_color, 6, verts);
        emit_cylinder(jp(Joint::R_HIP),  jp(Joint::R_KNEE),  limb_r,        cloth_color, 6, verts);
        emit_cylinder(jp(Joint::R_KNEE), jp(Joint::R_ANKLE), limb_r * 0.85f, cloth_color, 6, verts);
    });

    if (verts.empty()) return 0;

    uint32_t count = (uint32_t)std::min(verts.size(), (size_t)MAX_ACTOR_VERTICES);
    uint32_t byte_size = count * (uint32_t)sizeof(BasaltVertex);

    // Map transfer buffer, copy geometry.
    void *ptr = SDL_MapGPUTransferBuffer(gpu_device, transfer_buf, true);
    if (!ptr) return 0;
    std::memcpy(ptr, verts.data(), byte_size);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buf);

    // Copy transfer → vertex buffer.
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { transfer_buf, 0 };
    SDL_GPUBufferRegion           dst = { actor_vbo, 0, byte_size };
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);

    return count;
}

void ActorRenderer::draw(SDL_GPURenderPass *pass,
                          SDL_GPUCommandBuffer *cmd,
                          const SceneUniforms &uniforms,
                          SDL_GPUBuffer *point_light_ssbo,
                          uint32_t vertex_count) {
    if (!initialized || !pipeline || !actor_vbo || vertex_count == 0) return;

    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    SDL_PushGPUVertexUniformData(cmd,   0, &uniforms, sizeof(SceneUniforms));
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));

    SDL_GPUBuffer *frag_storage[3] = {
        point_light_ssbo ? point_light_ssbo : dummy_ssbo_,
        dummy_ssbo_,
        dummy_ssbo_,
    };
    SDL_BindGPUFragmentStorageBuffers(pass, 0, frag_storage, 3);

    SDL_GPUBufferBinding vbind = { actor_vbo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);

    SDL_DrawGPUPrimitives(pass, vertex_count, 1, 0, 0);
}
