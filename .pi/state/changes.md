I'll analyze the actor renderer implementation and related files before implementing the improvements.
<tool_call>
{"name": "mcp", "arguments": {"tool": "code_find_definition", "args": "{\"symbol\": \"ActorRenderer\", \"file\": \"src/game/render/actor_renderer.cpp\"}"}}
</tool_call>
<tool_response>
File: src/game/render/actor_renderer.cpp
```cpp
#include "actor_renderer.h"
#include "../actor.h"
#include "terrain/terrain_mesh.h"
#include "gpu/gpu.h"
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL3/SDL_gpu.h>
#include <cmath>
#include <vector>

// ─── geometry helpers ───────────────────────────────────────────────────────

void ActorRenderer::emit_cylinder(
    const glm::vec3 &a, const glm::vec3 &b,
    float radius, glm::vec3 color, int sides,
    std::vector<BasaltVertex> &verts)
{
    glm::vec3 axis = b - a;
    float len = glm::length(axis);
    if (len < 1e-4f) return;
    axis /= len;

    // Build orthonormal frame
    glm::vec3 up = std::abs(axis.z) < 0.9f ? glm::vec3(0,0,1) : glm::vec3(1,0,0);
    glm::vec3 u  = glm::normalize(glm::cross(axis, up));
    glm::vec3 v  = glm::cross(axis, u);

    auto ring = [&](glm::vec3 center, glm::vec3 norm_dir) {
        std::vector<glm::vec3> pts(sides);
        for (int i = 0; i < sides; ++i) {
            float angle = (float)i / sides * 2.0f * M_PI;
            pts[i] = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
        }
        return pts;
    };

    auto top_ring = ring(a, -axis);
    auto bot_ring = ring(b,  axis);

    for (int i = 0; i < sides; ++i) {
        int ni = (i + 1) % sides;

        glm::vec3 t0 = top_ring[i], t1 = top_ring[ni];
        glm::vec3 b0 = bot_ring[i], b1 = bot_ring[ni];
        glm::vec3 n0 = glm::normalize(top_ring[i] - a);
        glm::vec3 n1 = glm::normalize(top_ring[ni] - a);

        auto V = [&](glm::vec3 p, glm::vec3 n) -> BasaltVertex {
            return { p.x, p.y, p.z, color.x, color.y, color.z, 0.0f, n.x, n.y, n.z };
        };

        verts.push_back(V(t0, n0));
        verts.push_back(V(b0, n0));
        verts.push_back(V(t1, n1));

        verts.push_back(V(t1, n1));
        verts.push_back(V(b0, n0));
        verts.push_back(V(b1, n1));
    }
}

// ─── init / cleanup ─────────────────────────────────────────────────────────

void ActorRenderer::init(SDL_GPUDevice *device,
                         SDL_GPUGraphicsPipeline *terrain_pipeline,
                         SDL_GPUBuffer *dummy_ssbo,
                         AssetManager *am)
{
    gpu_device    = device;
    pipeline      = terrain_pipeline;
    dummy_ssbo_   = dummy_ssbo;
    asset_manager = am;

    // Static-sized VBO
    SDL_GPUBufferCreateInfo vbo_info{};
    vbo_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vbo_info.size  = MAX_ACTOR_VERTICES * sizeof(BasaltVertex);
    actor_vbo = SDL_CreateGPUBuffer(device, &vbo_info);

    // Persistent mapped staging buffer
    SDL_GPUTransferBufferCreateInfo tb_info{};
    tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb_info.size  = MAX_ACTOR_VERTICES * sizeof(BasaltVertex);
    transfer_buf = SDL_CreateGPUTransferBuffer(device, &tb_info);

    initialized = true;
}

void ActorRenderer::cleanup(SDL_GPUDevice *device)
{
    if (!initialized) return;
    SDL_ReleaseGPUBuffer(device, actor_vbo);
    SDL_ReleaseGPUTransferBuffer(device, transfer_buf);
    initialized = false;
}

// ─── per-frame build & upload ────────────────────────────────────────────────

static glm::vec3 compute_skeleton(
    const Transform &tf,
    const ActorConfig &cfg,
    const LegState &legs,
    glm::vec3 out_joints[(int)Joint::COUNT])
{
    using J = Joint;

    // Character facing direction (unit vector in XY plane)
    float fwd_x = std::cos(tf.facing);
    float fwd_y = std::sin(tf.facing);
    float right_x = -fwd_y, right_y = fwd_x;

    // Base hip position (root is at ground level)
    float hip_h = cfg.leg_len + cfg.shin_len;
    glm::vec3 hip = { tf.x, tf.y, tf.z + hip_h };

    // Root
    out_joints[(int)J::ROOT] = { tf.x, tf.y, tf.z };

    // Spine chain (hip → spine → chest → neck → head)
    glm::vec3 chest = hip + glm::vec3(0, 0, cfg.torso_len);
    glm::vec3 neck  = chest + glm::vec3(0, 0, cfg.neck_len);
    glm::vec3 head  = neck  + glm::vec3(0, 0, cfg.head_radius);

    out_joints[(int)J::SPINE] = hip;
    out_joints[(int)J::CHEST] = chest;
    out_joints[(int)J::NECK]  = neck;
    out_joints[(int)J::HEAD]  = head;

    // Shoulders
    glm::vec3 sh_l = chest + glm::vec3(-right_x, -right_y, 0) * cfg.shoulder_width;
    glm::vec3 sh_r = chest + glm::vec3( right_x,  right_y, 0) * cfg.shoulder_width;
    out_joints[(int)J::L_SHOULDER] = sh_l;
    out_joints[(int)J::R_SHOULDER] = sh_r;

    // Arms hang down from shoulders with slight forward angle when idle
    glm::vec3 elbow_l = sh_l + glm::vec3(0, 0, -cfg.arm_len);
    glm::vec3 wrist_l = elbow_l + glm::vec3(0, 0, -cfg.forearm_len);
    glm::vec3 elbow_r = sh_r + glm::vec3(0, 0, -cfg.arm_len);
    glm::vec3 wrist_r = elbow_r + glm::vec3(0, 0, -cfg.forearm_len);
    out_joints[(int)J::L_ELBOW] = elbow_l;
    out_joints[(int)J::L_WRIST] = wrist_l;
    out_joints[(int)J::R_ELBOW] = elbow_r;
    out_joints[(int)J::R_WRIST] = wrist_r;

    // Hip sockets
    glm::vec3 sock_l = hip + glm::vec3(-right_x, -right_y, 0) * cfg.hip_width;
    glm::vec3 sock_r = hip + glm::vec3( right_x,  right_y, 0) * cfg.hip_width;
    out_joints[(int)J::L_HIP] = sock_l;
    out_joints[(int)J::R_HIP] = sock_r;

    // Legs: use IK foot positions from LegState
    // Two-bone IK for each leg
    auto solve_leg = [&](glm::vec3 root, glm::vec3 foot_target, float upper_len, float lower_len,
                         glm::vec3 &knee_out) {
        glm::vec3 delta = foot_target - root;
        float dist = glm::length(delta);
        float max_reach = upper_len + lower_len - 0.001f;
        if (dist > max_reach) {
            delta = glm::normalize(delta) * max_reach;
            dist  = max_reach;
        }
        // Law of cosines for knee angle
        float cos_a = (dist*dist + upper_len*upper_len - lower_len*lower_len)
                      / (2.0f * dist * upper_len);
        cos_a = glm::clamp(cos_a, -1.0f, 1.0f);
        float a = std::acos(cos_a);

        // Knee hint: slightly forward in facing direction
        glm::vec3 hint = glm::normalize(delta + glm::vec3(fwd_x, fwd_y, 0) * 0.3f);
        glm::vec3 perp = glm::cross(hint, glm::vec3(0, 0, 1));
        if (glm::length2(perp) < 1e-6f) perp = glm::vec3(fwd_x, fwd_y, 0);
        perp = glm::normalize(perp);

        // Rotate delta direction by angle a around perp
        // Use Rodrigues' rotation
        glm::vec3 dir = glm::normalize(delta);
        glm::vec3 rotated = dir * std::cos(a)
                          + glm::cross(perp, dir) * std::sin(a)
                          + perp * glm::dot(perp, dir) * (1 - std::cos(a));
        knee_out = root + rotated * upper_len;
    };

    glm::vec3 knee_l, knee_r;
    solve_leg(sock_l, legs.foot[0], cfg.leg_len, cfg.shin_len, knee_l);
    solve_leg(sock_r, legs.foot[1], cfg.leg_len, cfg.shin_len, knee_r);

    out_joints[(int)J::L_KNEE]  = knee_l;
    out_joints[(int)J::L_ANKLE] = legs.foot[0];
    out_joints[(int)J::R_KNEE]  = knee_r;
    out_joints[(int)J::R_ANKLE] = legs.foot[1];

    return hip;
}

// ─── arm swing during walk ───────────────────────────────────────────────────

static void compute_arm_swing(
    const Transform &tf,
    const ActorConfig &cfg,
    const ProceduralGait &gait,
    const glm::vec3 &chest,
    const glm::vec3 &sh_l,
    const glm::vec3 &sh_r,
    glm::vec3 &elbow_l, glm::vec3 &wrist_l,
    glm::vec3 &elbow_r, glm::vec3 &wrist_r)
{
    float fwd_x = std::cos(tf.facing);
    float fwd_y = std::sin(tf.facing);

    // Arms swing opposite to leg phase
    float swing_l =  std::sin(gait.phase) * 0.4f; // rad
    float swing_r = -std::sin(gait.phase) * 0.4f;

    auto arm_end = [&](glm::vec3 sh, float swing, float side_sign) {
        // Upper arm swings forward/back
        glm::vec3 upper_dir = glm::vec3(-fwd_x * swing, -fwd_y * swing, -1.0f);
        upper_dir = glm::normalize(upper_dir);
        glm::vec3 elbow = sh + upper_dir * cfg.arm_len;

        // Forearm hangs slightly inward
        glm::vec3 fore_dir = upper_dir + glm::vec3(fwd_x * 0.1f, fwd_y * 0.1f, -0.2f);
        fore_dir = glm::normalize(fore_dir);
        glm::vec3 wrist = elbow + fore_dir * cfg.forearm_len;
        return std::make_pair(elbow, wrist);
    };

    auto [el, wl] = arm_end(sh_l, swing_l, -1.0f);
    auto [er, wr] = arm_end(sh_r, swing_r,  1.0f);
    elbow_l = el; wrist_l = wl;
    elbow_r = er; wrist_r = wr;
}

uint32_t ActorRenderer::prepare(SDL_GPUCommandBuffer *cmd, flecs::world &ecs)
{
    std::vector<BasaltVertex> verts;
    verts.reserve(4096);

    ecs.each<Transform, ActorConfig, ProceduralGait, LegState>(
        [&](Transform &tf, ActorConfig &cfg, ProceduralGait &gait, LegState &legs)
    {
        // Compute skeleton joint positions
        glm::vec3 joints[(int)Joint::COUNT];
        glm::vec3 hip = compute_skeleton(tf, cfg, legs, joints);

        using J = Joint;
        glm::vec3 sh_l = joints[(int)J::L_SHOULDER];
        glm::vec3 sh_r = joints[(int)J::R_SHOULDER];

        // Recompute arms with swing if moving
        glm::vec3 el_l = joints[(int)J::L_ELBOW];
        glm::vec3 wr_l = joints[(int)J::L_WRIST];
        glm::vec3 el_r = joints[(int)J::R_ELBOW];
        glm::vec3 wr_r = joints[(int)J::R_WRIST];

        if (gait.phase != 0.0f) { // crude "is moving" check
            compute_arm_swing(tf, cfg, gait, hip, sh_l, sh_r,
                              el_l, wr_l, el_r, wr_r);
        }

        // Skin colors
        glm::vec3 skin  = { 0.78f, 0.60f, 0.45f };
        glm::vec3 shirt = { 0.25f, 0.35f, 0.55f };
        glm::vec3 pants = { 0.20f, 0.18f, 0.28f };
        glm::vec3 shoe  = { 0.15f, 0.12f, 0.10f };

        int seg = 7; // cylinder sides

        // Torso
        emit_cylinder(joints[(int)J::SPINE], joints[(int)J::CHEST],
                      cfg.torso_radius, shirt, seg, verts);
        // Neck
        emit_cylinder(joints[(int)J::CHEST], joints[(int)J::NECK],
                      cfg.limb_radius * 0.8f, skin, seg, verts);
        // Head sphere (approx with cylinder stub)
        emit_cylinder(joints[(int)J::NECK],
                      joints[(int)J::HEAD] + glm::vec3(0,0,cfg.head_radius),
                      cfg.head_radius, skin, seg+2, verts);

        // Arms
        emit_cylinder(sh_l, el_l, cfg.limb_radius, shirt, seg, verts);
        emit_cylinder(el_l, wr_l, cfg.limb_radius * 0.85f, skin, seg, verts);
        emit_cylinder(sh_r, el_r, cfg.limb_radius, shirt, seg, verts);
        emit_cylinder(el_r, wr_r, cfg.limb_radius * 0.85f, skin, seg, verts);

        // Legs
        emit_cylinder(joints[(int)J::L_HIP], joints[(int)J::L_KNEE],
                      cfg.limb_radius * 1.1f, pants, seg, verts);
        emit_cylinder(joints[(int)J::L_KNEE], joints[(int)J::L_ANKLE],
                      cfg.limb_radius, pants, seg, verts);
        emit_cylinder(joints[(int)J::R_HIP], joints[(int)J::R_KNEE],
                      cfg.limb_radius * 1.1f, pants, seg, verts);
        emit_cylinder(joints[(int)J::R_KNEE], joints[(int)J::R_ANKLE],
                      cfg.limb_radius, pants, seg, verts);

        // Feet (short stub)
        glm::vec3 fwd3 = { std::cos(tf.facing), std::sin(tf.facing), 0 };
        emit_cylinder(joints[(int)J::L_ANKLE],
                      joints[(int)J::L_ANKLE] + fwd3 * cfg.shin_len * 0.35f + glm::vec3(0,0,-0.05f),
                      cfg.limb_radius * 1.1f, shoe, seg, verts);
        emit_cylinder(joints[(int)J::R_ANKLE],
                      joints[(int)J::R_ANKLE] + fwd3 * cfg.shin_len * 0.35f + glm::vec3(0,0,-0.05f),
                      cfg.limb_radius * 1.1f, shoe, seg, verts);
    });

    if (verts.empty()) return 0;

    // Upload
    uint32_t byte_count = (uint32_t)(verts.size() * sizeof(BasaltVertex));
    void *mapped = SDL_MapGPUTransferBuffer(gpu_device, transfer_buf, true);
    std::memcpy(mapped, verts.data(), byte_count);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buf);

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{ transfer_buf, 0 };
    SDL_GPUBufferRegion dst{ actor_vbo, 0, byte_count };
    SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    SDL_EndGPUCopyPass(cp);

    return (uint32_t)verts.size();
}

// ─── draw ────────────────────────────────────────────────────────────────────

void ActorRenderer::draw(SDL_GPURenderPass *pass,
                         SDL_GPUCommandBuffer *cmd,
                         const SceneUniforms &uniforms,
                         SDL_GPUBuffer *point_light_ssbo,
                         uint32_t vertex_count)
{
    if (!initialized || vertex_count == 0) return;

    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    SDL_GPUBufferBinding vb{ actor_vbo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

    SDL_GPUStorageBufferBinding ssbo_bindings[3] = {
        { point_light_ssbo, 0 },
        { dummy_ssbo_,      0 },
        { dummy_ssbo_,      0 }
    };
    SDL_BindGPUFragmentStorageBuffers(pass, 0, ssbo_bindings, 3);

    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));

    SDL_DrawGPUPrimitives(pass, vertex_count, 1, 0, 0);
}
```
</tool_response>

<tool_call>
{"name": "mcp", "arguments": {"tool": "code_find_definition", "args": "{\"symbol\": \"topo_game\", \"file\": \"src/game/topo_game.cpp\"}"}}
</tool_call>
<tool_response>
File: src/game/topo_game.cpp
```cpp
#include "topo_game.h"
#include "actor.h"
#include "game_state.h"
#include "terrain/terrain_generator.h"
#include "terrain/terrain_renderer.h"
#include "terrain/terrain_mesh.h"
#include "terrain/isometric.h"
#include "terrain/hex.h"
#include <imgui.h>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

// ── helpers ───────────────────────────────────────────────────────────────────

static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// ── terrain surface query ─────────────────────────────────────────────────────

static float sample_terrain_z(const GameState &gs, float wx, float wy) {
    if (!gs.terrain_ready) return 0.0f;
    const auto &mesh = gs.terrain_mesh;
    if (mesh.basalt_layers.empty()) return 0.0f;

    float best_z = 0.0f;
    float best_dist = 1e9f;

    for (auto &layer : mesh.basalt_layers) {
        // Quick bounds check
        if (wx < layer.bounds_min.x - 20 || wx > layer.bounds_max.x + 20 ||
            wy < layer.bounds_min.y - 20 || wy > layer.bounds_max.y + 20)
            continue;

        const auto *verts = layer.vertices.data();
        for (size_t i = 0; i + 2 < layer.vertices.size(); i += 3) {
            const auto &v0 = verts[i];
            const auto &v1 = verts[i+1];
            const auto &v2 = verts[i+2];

            // Point-in-triangle (XY)
            float d1 = (wx - v1.pos_x) * (v0.pos_y - v1.pos_y) -
                       (v0.pos_x - v1.pos_x) * (wy - v1.pos_y);
            float d2 = (wx - v2.pos_x) * (v1.pos_y - v2.pos_y) -
                       (v1.pos_x - v2.pos_x) * (wy - v2.pos_y);
            float d3 = (wx - v0.pos_x) * (v2.pos_y - v0.pos_y) -
                       (v2.pos_x - v0.pos_x) * (wy - v0.pos_y);

            bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (has_neg && has_pos) continue; // outside triangle

            // Barycentric interpolation for Z
            float denom = (v1.pos_y - v2.pos_y) * (v0.pos_x - v2.pos_x) +
                          (v2.pos_x - v1.pos_x) * (v0.pos_y - v2.pos_y);
            if (std::abs(denom) < 1e-6f) continue;

            float w0 = ((v1.pos_y - v2.pos_y) * (wx - v2.pos_x) +
                        (v2.pos_x - v1.pos_x) * (wy - v2.pos_y)) / denom;
            float w1 = ((v2.pos_y - v0.pos_y) * (wx - v2.pos_x) +
                        (v0.pos_x - v2.pos_x) * (wy - v2.pos_y)) / denom;
            float w2 = 1.0f - w0 - w1;

            float z = w0 * v0.pos_z + w1 * v1.pos_z + w2 * v2.pos_z;
            if (z > best_z || best_dist > 1.0f) {
                best_z   = z;
                best_dist = 0.0f;
            }
        }
    }
    return best_z;
}

// ── leg state update ──────────────────────────────────────────────────────────

static void update_legs(LegState &legs,
                        const Transform &tf,
                        const ActorConfig &cfg,
                        ProceduralGait &gait,
                        const GameState &gs,
                        float dt,
                        bool moving)
{
    float fwd_x = std::cos(tf.facing);
    float fwd_y = std::sin(tf.facing);
    float right_x = -fwd_y, right_y = fwd_x;

    float hip_h = cfg.leg_len + cfg.shin_len;
    glm::vec3 hip = { tf.x, tf.y, tf.z + hip_h };
    glm::vec3 sock_l = hip + glm::vec3(-right_x, -right_y, 0) * cfg.hip_width;
    glm::vec3 sock_r = hip + glm::vec3( right_x,  right_y, 0) * cfg.hip_width;

    // Default foot positions directly below hip sockets (on terrain)
    float terrain_l = sample_terrain_z(gs, sock_l.x, sock_l.y);
    float terrain_r = sample_terrain_z(gs, sock_r.x, sock_r.y);

    glm::vec3 def_l = { sock_l.x, sock_l.y, terrain_l };
    glm::vec3 def_r = { sock_r.x, sock_r.y, terrain_r };

    if (!moving) {
        // Blend feet back to default
        legs.foot[0] = glm::mix(legs.foot[0], def_l, std::min(1.0f, dt * 8.0f));
        legs.foot[1] = glm::mix(legs.foot[1], def_r, std::min(1.0f, dt * 8.0f));
        gait.phase = 0.0f;
        return;
    }

    // Advance phase
    float phase_speed = (2.0f * M_PI) / (2.0f * gait.step_duration);
    gait.phase += phase_speed * dt;
    if (gait.phase > 2.0f * M_PI) gait.phase -= 2.0f * M_PI;

    // Stride targets: ahead of hip socket in walk direction
    float stride_fwd = gait.stride_len * 0.5f;
    glm::vec3 tgt_l = {
        sock_l.x + fwd_x * stride_fwd,
        sock_l.y + fwd_y * stride_fwd,
        sample_terrain_z(gs, sock_l.x + fwd_x * stride_fwd, sock_l.y + fwd_y * stride_fwd)
    };
    glm::vec3 tgt_r = {
        sock_r.x + fwd_x * stride_fwd,
        sock_r.y + fwd_y * stride_fwd,
        sample_terrain_z(gs, sock_r.x + fwd_x * stride_fwd, sock_r.y + fwd_y * stride_fwd)
    };

    // Trigger steps: when foot is too far behind
    auto check_step = [&](int side, glm::vec3 &foot, glm::vec3 &prev, glm::vec3 &target,
                          float &progress, bool &stepping, glm::vec3 tgt, glm::vec3 sock) {
        if (!stepping) {
            float lag = glm::dot(sock - foot, {fwd_x, fwd_y, 0});
            if (lag > gait.stride_len * 0.6f) {
                stepping  = true;
                prev      = foot;
                target    = tgt;
                progress  = 0.0f;
            }
        } else {
            progress += dt / gait.step_duration;
            if (progress >= 1.0f) {
                progress = 1.0f;
                stepping = false;
            }
            float arc = std::sin(progress * M_PI) * gait.step_height;
            foot = glm::mix(prev, target, progress) + glm::vec3(0, 0, arc);
        }
    };

    check_step(0, legs.foot[0], legs.prev_foot[0], legs.target[0],
               legs.progress[0], legs.stepping[0], tgt_l, sock_l);
    check_step(1, legs.foot[1], legs.prev_foot[1], legs.target[1],
               legs.progress[1], legs.stepping[1], tgt_r, sock_r);
}

// ── TopoGame lifecycle ────────────────────────────────────────────────────────

void TopoGame::on_init(Application &app) {
    // ... (terrain renderer init, camera setup, etc.)
}

void TopoGame::on_event(Application &app, SDL_Event &ev) {
    // ... (event handling)
}

void TopoGame::on_render_game(Application &app, FrameContext &fc) {
    // ... (render terrain + actors)
}

void TopoGame::on_render_tool(Application &app, FrameContext &fc) {
    // ... (imgui tool window)
}
```
</tool_response>

<tool_call>
{"name": "mcp", "arguments": {"tool": "code_find_definition", "args": "{\"symbol\": \"on_init\", \"file\": \"src/game/topo_game.cpp\"}"}}
</tool_call>
<tool_response>
File: src/game/topo_game.cpp
```cpp
void TopoGame::on_init(Application &app) {
    // ── ECS setup ──────────────────────────────────────────────────────────
    ecs.set_threads(1);

    // Player entity
    auto player = ecs.entity("player")
        .add<Player>()
        .add<ActorTag>()
        .set<Transform>({ 512.0f, 512.0f, 0.0f, 0.0f })