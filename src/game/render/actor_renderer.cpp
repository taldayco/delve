#include "render/actor_renderer.h"
#include "actor.h"
#include "actor_animation.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>
#include <algorithm>

static constexpr float PI = 3.14159265358979323846f;

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

    glm::vec3 body_color(0.55f, 0.52f, 0.48f);

    ecs.each([&](ActorTag, const SkeletonPose &pose_in, const ActorConfig &cfg,
                 const Transform &t, const ProceduralGait &gait, const AnimationState &anim) {
        // Apply hip counter-animation on top of the already-posed skeleton.
        // Compute new-style values from gait phase for hip_bob_y and hip_rotation_deg.
        constexpr AnimationConfig anim_cfg{};
        ActorAnimationState hip_anim;
        // gait.phase is continuous radians; normalize to [0,1) for compute_hip_counter_animation.
        constexpr float TWO_PI = 2.0f * PI;
        float norm_phase = std::fmod(gait.phase, TWO_PI) / TWO_PI;
        if (norm_phase < 0.0f) norm_phase += 1.0f;
        hip_anim.stride_phase = norm_phase;
        compute_hip_counter_animation(hip_anim, anim_cfg);

        // Build a local mutable copy of the pose to apply hip transform.
        SkeletonPose pose = pose_in;

        // Vertical double-bounce: lift root and hips.
        pose.joints[(int)Joint::ROOT].z  += hip_anim.hip_bob_y;
        pose.joints[(int)Joint::SPINE].z += hip_anim.hip_bob_y * 0.5f;
        pose.joints[(int)Joint::L_HIP].z += hip_anim.hip_bob_y;
        pose.joints[(int)Joint::R_HIP].z += hip_anim.hip_bob_y;

        // Lateral drop: shift CoM toward stance side.
        float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);
        float drop_shift = hip_anim.hip_drop_fraction;
        glm::vec3 drop_vec(rght_x * drop_shift, rght_y * drop_shift, 0.0f);
        // hip_rotation_deg sign: positive = left foot back = shift right.
        float side = (hip_anim.hip_rotation_deg >= 0.0f) ? 1.0f : -1.0f;
        pose.joints[(int)Joint::ROOT]  += drop_vec * side;
        pose.joints[(int)Joint::L_HIP] += drop_vec * side;
        pose.joints[(int)Joint::R_HIP] += drop_vec * side;

        // Apply isometric height foreshortening: compress character vertical extent
        // relative to foot/terrain level so actor reads proportional in 2:1 iso view.
        // Feet stay planted on terrain; everything above scales by ISO_CHAR_HEIGHT_SCALE.
        {
            float foot_z = t.z - cfg.leg_len - cfg.shin_len;
            for (int ji = 0; ji < (int)Joint::COUNT; ++ji) {
                pose.joints[ji].z = foot_z + (pose.joints[ji].z - foot_z) * AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
            }
        }

        using J = Joint;
        auto j = [&](J jt) -> const glm::vec3 & { return pose.joints[(int)jt]; };

        int limb  = 6;
        int torso = 4;

        emit_cylinder(j(J::ROOT),  j(J::SPINE), cfg.torso_radius, body_color, torso, verts);
        emit_cylinder(j(J::SPINE), j(J::CHEST), cfg.torso_radius, body_color, torso, verts);
        emit_cylinder(j(J::CHEST), j(J::NECK),  cfg.torso_radius, body_color, torso, verts);
        emit_cylinder(j(J::NECK),  j(J::HEAD),  cfg.head_radius,  body_color, torso, verts);

        emit_cylinder(j(J::CHEST),      j(J::L_SHOULDER), cfg.limb_radius, body_color, limb, verts);
        emit_cylinder(j(J::L_SHOULDER), j(J::L_ELBOW),    cfg.limb_radius, body_color, limb, verts);
        emit_cylinder(j(J::L_ELBOW),    j(J::L_WRIST),    cfg.limb_radius, body_color, limb, verts);

        emit_cylinder(j(J::CHEST),      j(J::R_SHOULDER), cfg.limb_radius, body_color, limb, verts);
        emit_cylinder(j(J::R_SHOULDER), j(J::R_ELBOW),    cfg.limb_radius, body_color, limb, verts);
        emit_cylinder(j(J::R_ELBOW),    j(J::R_WRIST),    cfg.limb_radius, body_color, limb, verts);

        emit_cylinder(j(J::ROOT),   j(J::L_HIP),   cfg.limb_radius, body_color, limb, verts);
        emit_cylinder(j(J::L_HIP),  j(J::L_KNEE),  cfg.limb_radius, body_color, limb, verts);
        emit_cylinder(j(J::L_KNEE), j(J::L_ANKLE), cfg.limb_radius, body_color, limb, verts);

        emit_cylinder(j(J::ROOT),   j(J::R_HIP),   cfg.limb_radius, body_color, limb, verts);
        emit_cylinder(j(J::R_HIP),  j(J::R_KNEE),  cfg.limb_radius, body_color, limb, verts);
        emit_cylinder(j(J::R_KNEE), j(J::R_ANKLE), cfg.limb_radius, body_color, limb, verts);
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
