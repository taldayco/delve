#include "render/actor_renderer.h"
#include "actor.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>

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

    SDL_GPUBufferCreateInfo bi = {};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size  = MAX_ACTOR_VERTICES * sizeof(BasaltVertex);
    actor_vbo = SDL_CreateGPUBuffer(device, &bi);
    if (!actor_vbo) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ActorRenderer: Failed to create vertex buffer: %s", SDL_GetError());
        return;
    }

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

    // Skeleton GPU buffers.
    SDL_GPUBufferCreateInfo skel_vbi = {};
    skel_vbi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    skel_vbi.size  = MAX_SKEL_VERTICES * sizeof(SkeletonVertex);
    skel_vbo = SDL_CreateGPUBuffer(device, &skel_vbi);

    SDL_GPUBufferCreateInfo skel_ibi = {};
    skel_ibi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    skel_ibi.size  = MAX_SKEL_INDICES * sizeof(uint32_t);
    skel_ibo = SDL_CreateGPUBuffer(device, &skel_ibi);

    uint32_t skel_transfer_size = MAX_SKEL_VERTICES * sizeof(SkeletonVertex)
                                + MAX_SKEL_INDICES  * sizeof(uint32_t);
    SDL_GPUTransferBufferCreateInfo stbi = {};
    stbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    stbi.size  = skel_transfer_size;
    skel_transfer = SDL_CreateGPUTransferBuffer(device, &stbi);

    if (!skel_vbo || !skel_ibo || !skel_transfer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ActorRenderer: Failed to create skeleton GPU buffers: %s", SDL_GetError());
        if (skel_vbo)      { SDL_ReleaseGPUBuffer(device, skel_vbo);              skel_vbo = nullptr; }
        if (skel_ibo)      { SDL_ReleaseGPUBuffer(device, skel_ibo);              skel_ibo = nullptr; }
        if (skel_transfer) { SDL_ReleaseGPUTransferBuffer(device, skel_transfer); skel_transfer = nullptr; }
    }

    // Default bone colors per BoneSeg.
    glm::vec4 torso_color(0.45f, 0.42f, 0.38f, 1.0f);
    glm::vec4 limb_color (0.55f, 0.52f, 0.48f, 1.0f);
    for (int i = 0; i < (int)BoneSeg::COUNT; ++i) {
        bool torso = (i <= (int)BoneSeg::HEAD_SEG);
        skel_bone_colors[i] = torso ? torso_color : limb_color;
    }

    initialized = true;
    SDL_Log("ActorRenderer: Initialized (VBO capacity: %u vertices)", MAX_ACTOR_VERTICES);
}

void ActorRenderer::init_skel_pipeline(SDL_GPUDevice *device,
                                        SDL_Window *window,
                                        SDL_GPUTextureFormat depth_fmt) {
    if (skel_pipeline || !asset_manager) return;

    std::string shader_dir = SHADER_DIR;

    // Vertex shader: 1 uniform buffer (SceneUniforms at slot 0 = set1,binding0).
    // No bone matrices — CPU LBS writes world-space positions before upload.
    SDL_GPUShader *vert = asset_manager->load_shader(
        "actor_skel.vert",
        shader_dir + "/actor_skel.vert.glsl.spv",
        SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
    // Fragment shader: 1 uniform buffer (BoneColors at slot 0).
    SDL_GPUShader *frag = asset_manager->load_shader(
        "actor_skel.frag",
        shader_dir + "/actor_skel.frag.glsl.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

    if (!vert || !frag) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ActorRenderer: Failed to load skeleton shaders");
        if (vert) SDL_ReleaseGPUShader(device, vert);
        if (frag) SDL_ReleaseGPUShader(device, frag);
        return;
    }

    SDL_GPUTextureFormat swapchain_fmt = SDL_GetGPUSwapchainTextureFormat(device, window);

    // Vertex layout: matches SkeletonVertex (36 bytes).
    // position(vec3) @ 0, normal(vec3) @ 12, bone_index0(float) @ 24,
    // bone_weight(float) @ 28, bone_index1(float) @ 32.
    SDL_GPUVertexBufferDescription vbuf = {};
    vbuf.slot       = 0;
    vbuf.pitch      = sizeof(SkeletonVertex);
    vbuf.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attrs[5] = {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                 (Uint32)offsetof(SkeletonVertex, position)    };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                 (Uint32)offsetof(SkeletonVertex, normal)      };
    attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
                 (Uint32)offsetof(SkeletonVertex, bone_index0) };
    attrs[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
                 (Uint32)offsetof(SkeletonVertex, bone_weight) };
    attrs[4] = { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
                 (Uint32)offsetof(SkeletonVertex, bone_index1) };

    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader   = vert;
    pi.fragment_shader = frag;

    pi.vertex_input_state.vertex_buffer_descriptions = &vbuf;
    pi.vertex_input_state.num_vertex_buffers         = 1;
    pi.vertex_input_state.vertex_attributes          = attrs;
    pi.vertex_input_state.num_vertex_attributes      = 5;

    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pi.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_BACK;
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

    pi.depth_stencil_state.enable_depth_test  = true;
    pi.depth_stencil_state.enable_depth_write = true;
    pi.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = swapchain_fmt;
    pi.target_info.color_target_descriptions    = &color_target;
    pi.target_info.num_color_targets            = 1;
    pi.target_info.depth_stencil_format         = depth_fmt;
    pi.target_info.has_depth_stencil_target     = true;

    skel_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
    if (!skel_pipeline)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ActorRenderer: Failed to create skeleton pipeline: %s", SDL_GetError());
    else
        SDL_Log("ActorRenderer: Skeleton pipeline initialized");

    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);
}

void ActorRenderer::cleanup(SDL_GPUDevice *device) {
    if (actor_vbo)    { SDL_ReleaseGPUBuffer(device, actor_vbo);           actor_vbo    = nullptr; }
    if (transfer_buf) { SDL_ReleaseGPUTransferBuffer(device, transfer_buf); transfer_buf = nullptr; }
    if (skel_vbo)     { SDL_ReleaseGPUBuffer(device, skel_vbo);            skel_vbo     = nullptr; }
    if (skel_ibo)     { SDL_ReleaseGPUBuffer(device, skel_ibo);            skel_ibo     = nullptr; }
    if (skel_transfer){ SDL_ReleaseGPUTransferBuffer(device, skel_transfer); skel_transfer = nullptr; }
    if (skel_pipeline){ SDL_ReleaseGPUGraphicsPipeline(device, skel_pipeline); skel_pipeline = nullptr; }
    pipeline    = nullptr;
    dummy_ssbo_ = nullptr;
    initialized = false;
}

void ActorRenderer::emit_cylinder(const glm::vec3 &a, const glm::vec3 &b,
                                   float radius, glm::vec3 color, int sides,
                                   std::vector<BasaltVertex> &out_verts) {
    emit_cylinder_ex(a, b, radius, radius, color, sides, 0.0f, out_verts);
}

void ActorRenderer::emit_cylinder_ex(const glm::vec3 &a, const glm::vec3 &b,
                                      float r_start, float r_end,
                                      glm::vec3 color, int sides, float twist_rad,
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

    if (fabsf(twist_rad) > 1e-5f) {
        float c = cosf(twist_rad), s = sinf(twist_rad);
        glm::vec3 fwd_tmp = glm::cross(up, right);
        right = right * c + fwd_tmp * s;
        right = glm::normalize(right);
    }

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

        glm::vec3 r0 = (right * cosf(angle0) + fwd * sinf(angle0));
        glm::vec3 r1 = (right * cosf(angle1) + fwd * sinf(angle1));

        glm::vec3 p00 = a + r0 * r_start;
        glm::vec3 p10 = a + r1 * r_start;
        glm::vec3 p01 = b + r0 * r_end;
        glm::vec3 p11 = b + r1 * r_end;

        glm::vec3 face_n = glm::normalize(glm::cross(p10 - p00, p01 - p00));

        vert(p00, face_n); vert(p10, face_n); vert(p01, face_n);
        vert(p10, face_n); vert(p11, face_n); vert(p01, face_n);

        glm::vec3 bot_n = -up;
        vert(a, bot_n); vert(a + r1 * r_start, bot_n); vert(a + r0 * r_start, bot_n);

        glm::vec3 top_n = up;
        vert(b, top_n); vert(b + r0 * r_end, top_n); vert(b + r1 * r_end, top_n);
    }
}

void ActorRenderer::regenerate(const SkeletonPose &pose, SegmentProfiles &profiles) {
    using J = Joint;
    auto j = [&](J jt) -> const glm::vec3 & { return pose.joints[(int)jt]; };

    glm::vec3 body_color(0.55f, 0.52f, 0.48f);

    std::vector<BasaltVertex> verts;
    verts.reserve(regen_verts_.capacity() > 0 ? regen_verts_.capacity() : 512);

    struct SegDef { J a; J b; BoneSeg seg; };
    static const SegDef segs[] = {
        { J::ROOT,       J::SPINE,       BoneSeg::SPINE           },
        { J::SPINE,      J::CHEST,       BoneSeg::CHEST_CORE      },
        { J::CHEST,      J::NECK,        BoneSeg::NECK_SEG        },
        { J::NECK,       J::HEAD,        BoneSeg::HEAD_SEG        },
        { J::CHEST,      J::L_SHOULDER,  BoneSeg::L_SHOULDER_CONN },
        { J::L_SHOULDER, J::L_ELBOW,     BoneSeg::L_UPPER_ARM     },
        { J::L_ELBOW,    J::L_WRIST,     BoneSeg::L_FOREARM       },
        { J::CHEST,      J::R_SHOULDER,  BoneSeg::R_SHOULDER_CONN },
        { J::R_SHOULDER, J::R_ELBOW,     BoneSeg::R_UPPER_ARM     },
        { J::R_ELBOW,    J::R_WRIST,     BoneSeg::R_FOREARM       },
        { J::ROOT,       J::L_HIP,       BoneSeg::L_HIP_CONN      },
        { J::L_HIP,      J::L_KNEE,      BoneSeg::L_UPPER_LEG     },
        { J::L_KNEE,     J::L_ANKLE,     BoneSeg::L_LOWER_LEG     },
        { J::ROOT,       J::R_HIP,       BoneSeg::R_HIP_CONN      },
        { J::R_HIP,      J::R_KNEE,      BoneSeg::R_UPPER_LEG     },
        { J::R_KNEE,     J::R_ANKLE,     BoneSeg::R_LOWER_LEG     },
    };

    for (const auto &s : segs) {
        const BoneProfile &bp = profiles.bones[(int)s.seg];
        int sides = std::clamp(bp.sides, 3, 6);
        float twist_rad = bp.twist * (PI / 180.0f);
        emit_cylinder_ex(j(s.a), j(s.b),
                         bp.radius_start, bp.radius_end,
                         body_color, sides, twist_rad, verts);
    }

    uint32_t count = (uint32_t)std::min(verts.size(), (size_t)MAX_ACTOR_VERTICES);
    regen_verts_.resize(count);
    std::memcpy(regen_verts_.data(), verts.data(), count * sizeof(BasaltVertex));
    regen_vertex_count_ = count;
    regen_dirty_        = true;
}

void ActorRenderer::draw_ui(const SkeletonPose &pose) {
    if (!ImGui::CollapsingHeader("Bone Profiles"))
        return;

    static const char *seg_names[(int)BoneSeg::COUNT] = {
        "Spine",          "Chest Core",      "Neck",            "Head",
        "L Shoulder",     "L Upper Arm",     "L Forearm",
        "R Shoulder",     "R Upper Arm",     "R Forearm",
        "L Hip",          "L Upper Leg",     "L Lower Leg",
        "R Hip",          "R Upper Leg",     "R Lower Leg",
    };

    bool any_changed = false;

    for (int i = 0; i < (int)BoneSeg::COUNT; ++i) {
        BoneProfile &bp = segment_profiles.bones[i];

        ImGui::PushID(i);
        if (ImGui::TreeNodeEx(seg_names[i], ImGuiTreeNodeFlags_None)) {
            bool changed = false;
            changed |= ImGui::SliderFloat("radius_start", &bp.radius_start, 0.01f, 0.5f);
            changed |= ImGui::SliderFloat("radius_end",   &bp.radius_end,   0.01f, 0.5f);
            changed |= ImGui::SliderInt  ("sides",        &bp.sides,        3, 6);
            changed |= ImGui::SliderFloat("twist",        &bp.twist,       -180.0f, 180.0f);
            bp.sides = std::clamp(bp.sides, 3, 6);
            if (changed) any_changed = true;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (any_changed) {
        regenerate(pose, segment_profiles);
        skel_profiles_dirty_ = true;
    }
}

uint32_t ActorRenderer::prepare(SDL_GPUCommandBuffer *cmd, flecs::world &ecs) {
    if (!initialized || !actor_vbo || !transfer_buf) return 0;

    if (regen_dirty_ && regen_vertex_count_ > 0) {
        regen_dirty_ = false;

        uint32_t count     = regen_vertex_count_;
        uint32_t byte_size = count * (uint32_t)sizeof(BasaltVertex);

        void *ptr = SDL_MapGPUTransferBuffer(gpu_device, transfer_buf, true);
        if (!ptr) return 0;
        std::memcpy(ptr, regen_verts_.data(), byte_size);
        SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buf);

        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src = { transfer_buf, 0 };
        SDL_GPUBufferRegion           dst = { actor_vbo, 0, byte_size };
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);

        return count;
    }

    std::vector<BasaltVertex> verts;
    verts.reserve(512);

    glm::vec3 body_color(0.55f, 0.52f, 0.48f);

    ecs.each([&](ActorTag, const SkeletonPose &pose, const ActorConfig &cfg) {
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

    uint32_t count     = (uint32_t)std::min(verts.size(), (size_t)MAX_ACTOR_VERTICES);
    uint32_t byte_size = count * (uint32_t)sizeof(BasaltVertex);

    void *ptr = SDL_MapGPUTransferBuffer(gpu_device, transfer_buf, true);
    if (!ptr) return 0;
    std::memcpy(ptr, verts.data(), byte_size);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buf);

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { transfer_buf, 0 };
    SDL_GPUBufferRegion           dst = { actor_vbo, 0, byte_size };
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);

    return count;
}

void ActorRenderer::upload_mesh(SDL_GPUCommandBuffer *cmd, const SkeletonMesh &mesh) {
    if (!initialized || !skel_vbo || !skel_ibo || !skel_transfer) return;
    if (mesh.vertices.empty() || mesh.indices.empty()) return;

    uint32_t vc = (uint32_t)std::min(mesh.vertices.size(), (size_t)MAX_SKEL_VERTICES);
    uint32_t ic = (uint32_t)std::min(mesh.indices.size(),  (size_t)MAX_SKEL_INDICES);
    uint32_t vb = vc * (uint32_t)sizeof(SkeletonVertex);
    uint32_t ib = ic * (uint32_t)sizeof(uint32_t);

    uint8_t *ptr = (uint8_t *)SDL_MapGPUTransferBuffer(gpu_device, skel_transfer, true);
    if (!ptr) return;
    std::memcpy(ptr,      mesh.vertices.data(), vb);
    std::memcpy(ptr + vb, mesh.indices.data(),  ib);
    SDL_UnmapGPUTransferBuffer(gpu_device, skel_transfer);

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation vsrc = { skel_transfer, 0 };
    SDL_GPUBufferRegion           vdst = { skel_vbo, 0, vb };
    SDL_UploadToGPUBuffer(cp, &vsrc, &vdst, false);

    SDL_GPUTransferBufferLocation isrc = { skel_transfer, vb };
    SDL_GPUBufferRegion           idst = { skel_ibo, 0, ib };
    SDL_UploadToGPUBuffer(cp, &isrc, &idst, false);

    SDL_EndGPUCopyPass(cp);

    skel_index_count = ic;
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

void ActorRenderer::draw_skel(SDL_GPURenderPass *pass,
                               SDL_GPUCommandBuffer *cmd,
                               const SceneUniforms &uniforms) {
    if (!initialized || !skel_pipeline || !skel_vbo || !skel_ibo || skel_index_count == 0)
        return;

    SDL_BindGPUGraphicsPipeline(pass, skel_pipeline);

    // Vertex uniforms: slot 0 = SceneUniforms (view/projection).
    // No bone matrices — CPU LBS already wrote world-space positions.
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));

    // Fragment uniforms: slot 0 = BoneColors (per-bone tint).
    SDL_PushGPUFragmentUniformData(cmd, 0, skel_bone_colors, sizeof(skel_bone_colors));

    SDL_GPUBufferBinding vbind = { skel_vbo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);

    SDL_GPUBufferBinding ibind = { skel_ibo, 0 };
    SDL_BindGPUIndexBuffer(pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    SDL_DrawGPUIndexedPrimitives(pass, skel_index_count, 1, 0, 0, 0);
}
