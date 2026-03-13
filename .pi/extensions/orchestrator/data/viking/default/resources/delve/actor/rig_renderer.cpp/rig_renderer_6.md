// ---- IK goal wireframe boxes (green) ----
        float box_h = 0.04f;
        float edge_r = 0.008f;
        emit_wireframe_box(j(J::IK_FOOT_L), box_h, edge_r, ik_goal_color, verts);
        emit_wireframe_box(j(J::IK_FOOT_R), box_h, edge_r, ik_goal_color, verts);
        emit_wireframe_box(j(J::IK_HAND_L), box_h, edge_r, ik_goal_color, verts);
        emit_wireframe_box(j(J::IK_HAND_R), box_h, edge_r, ik_goal_color, verts);

        // ---- Pole target diamonds (orange) ----
        float diamond_r = 0.04f;
        emit_diamond(j(J::POLE_KNEE_L),  diamond_r, pole_color, verts);
        emit_diamond(j(J::POLE_KNEE_R),  diamond_r, pole_color, verts);
        emit_diamond(j(J::POLE_ELBOW_L), diamond_r, pole_color, verts);
        emit_diamond(j(J::POLE_ELBOW_R), diamond_r, pole_color, verts);

        // ---- Pole target lines (thin dashed cylinders from joint to pole) ----
        // Dashed: emit short segments alternating every 0.04f units along the line.
        auto emit_dashed = [&](const glm::vec3 &from, const glm::vec3 &to) {
            glm::vec3 dir = to - from;
            float total = glm::length(dir);
            if (total < 1e-5f) return;
            dir /= total;
            constexpr float seg_len = 0.04f;
            float t_cur = 0.0f;
            bool draw = true; // alternate on/off
            while (t_cur < total) {
                float t_next = std::min(t_cur + seg_len, total);
                if (draw) {
                    glm::vec3 seg_a = from + dir * t_cur;
                    glm::vec3 seg_b = from + dir * t_next;
                    emit_cylinder(seg_a, seg_b, 0.01f, pole_color, 4, verts);
                }
                t_cur = t_next;
                draw = !draw;
            }
        };

        emit_dashed(j(J::L_LOWER_LEG), j(J::POLE_KNEE_L));
        emit_dashed(j(J::R_LOWER_LEG), j(J::POLE_KNEE_R));
        emit_dashed(j(J::L_LOWER_ARM), j(J::POLE_ELBOW_L));
        emit_dashed(j(J::R_LOWER_ARM), j(J::POLE_ELBOW_R));
    });

    if (verts.empty()) return 0;

    uint32_t count = (uint32_t)std::min(verts.size(), (size_t)MAX_RIG_VERTICES);
    if (verts.size() > MAX_RIG_VERTICES) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "RigRenderer: vertex overflow — %zu clamped to %u",
                    verts.size(), MAX_RIG_VERTICES);
    }
    uint32_t byte_size = count * (uint32_t)sizeof(BasaltVertex);

    // Map transfer buffer, copy geometry.
    void *ptr = SDL_MapGPUTransferBuffer(gpu_device, transfer_buf, true);
    if (!ptr) return 0;
    std::memcpy(ptr, verts.data(), byte_size);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buf);

    // Copy transfer → vertex buffer.
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { transfer_buf, 0 };
    SDL_GPUBufferRegion           dst = { rig_vbo, 0, byte_size };
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);

    return count;
}

void RigRenderer::draw(SDL_GPURenderPass *pass,
                        SDL_GPUCommandBuffer *cmd,
                        const SceneUniforms &uniforms,
                        SDL_GPUBuffer *point_light_ssbo,
                        uint32_t vertex_count) {
    if (!initialized || !pipeline || !rig_vbo || vertex_count == 0) return;

    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    SDL_PushGPUVertexUniformData(cmd,   0, &uniforms, sizeof(SceneUniforms));
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(SceneUniforms));

    SDL_GPUBuffer *frag_storage[3] = {
        point_light_ssbo ? point_light_ssbo : dummy_ssbo_,
        dummy_ssbo_,
        dummy_ssbo_,
    };
    SDL_BindGPUFragmentStorageBuffers(pass, 0, frag_storage, 3);

    SDL_GPUBufferBinding vbind = { rig_vbo, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);

    SDL_DrawGPUPrimitives(pass, vertex_count, 1, 0, 0);
}