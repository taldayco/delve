void RigRenderer::emit_wireframe_box(const glm::vec3 &center, float half_size,
                                      float edge_radius, glm::vec3 color,
                                      std::vector<BasaltVertex> &out_verts) {
    float h = half_size;
    glm::vec3 c[8] = {
        {center.x - h, center.y - h, center.z - h},
        {center.x + h, center.y - h, center.z - h},
        {center.x + h, center.y + h, center.z - h},
        {center.x - h, center.y + h, center.z - h},
        {center.x - h, center.y - h, center.z + h},
        {center.x + h, center.y - h, center.z + h},
        {center.x + h, center.y + h, center.z + h},
        {center.x - h, center.y + h, center.z + h},
    };
    // 12 edges of a cube as thin cylinders
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},  // bottom face
        {4,5},{5,6},{6,7},{7,4},  // top face
        {0,4},{1,5},{2,6},{3,7},  // verticals
    };
    for (auto &e : edges)
        emit_cylinder(c[e[0]], c[e[1]], edge_radius, color, 4, out_verts);
}

uint32_t RigRenderer::prepare(SDL_GPUCommandBuffer *cmd, flecs::world &ecs) {
    if (!initialized || !rig_vbo || !transfer_buf) return 0;

    std::vector<BasaltVertex> verts;
    verts.reserve(16384);

    glm::vec3 body_color(0.55f, 0.52f, 0.48f);
    glm::vec3 ik_goal_color(0.2f, 0.8f, 0.2f);      // green for IK goals
    glm::vec3 pole_color(0.9f, 0.5f, 0.1f);          // orange for pole targets

    ecs.each([&](ActorTag, const RigPose &pose_in, const ActorConfig &cfg,
                 const Transform &t, const ProceduralGait &gait, const RigState &anim,
                 const ProceduralMesh &mesh) {
        // Apply hip counter-animation on top of the already-posed skeleton.
        constexpr AnimationConfig anim_cfg{};
        RigHipState hip_anim;
        constexpr float TWO_PI = 2.0f * PI;
        float norm_phase = std::fmod(gait.phase, TWO_PI) / TWO_PI;
        if (norm_phase < 0.0f) norm_phase += 1.0f;
        hip_anim.stride_phase = norm_phase;
        compute_rig_hip_state(hip_anim, anim_cfg);

        // Build a local mutable copy of the pose to apply hip transform.
        RigPose pose = pose_in;

        // Vertical double-bounce: lift hips and upper legs.
        pose.joints[(int)Joint::HIPS].z       += hip_anim.hip_bob_y;
        pose.joints[(int)Joint::SPINE_01].z   += hip_anim.hip_bob_y * 0.5f;
        pose.joints[(int)Joint::L_UPPER_LEG].z += hip_anim.hip_bob_y;
        pose.joints[(int)Joint::R_UPPER_LEG].z += hip_anim.hip_bob_y;

        // Lateral drop: shift CoM toward stance side.
        float rght_x = -sinf(anim.visual_facing), rght_y = cosf(anim.visual_facing);
        float drop_shift = hip_anim.hip_drop_fraction;
        glm::vec3 drop_vec(rght_x * drop_shift, rght_y * drop_shift, 0.0f);
        float side = (hip_anim.hip_rotation_deg >= 0.0f) ? 1.0f : -1.0f;
        pose.joints[(int)Joint::HIPS]        += drop_vec * side;
        pose.joints[(int)Joint::L_UPPER_LEG] += drop_vec * side;
        pose.joints[(int)Joint::R_UPPER_LEG] += drop_vec * side;

        // Apply isometric height foreshortening.
        {
            float foot_z = t.z;

            // Save ground-contact joints — must stay at terrain level
            using J = Joint;
            constexpr int ground_joints[] = {
                (int)J::L_FOOT, (int)J::R_FOOT,
                (int)J::L_TOE,  (int)J::R_TOE,
                (int)J::ROOT
            };
            float saved[5];
            for (int k = 0; k < 5; ++k) saved[k] = pose.joints[ground_joints[k]].z;

            for (int ji = 0; ji < (int)J::COUNT; ++ji) {
                pose.joints[ji].z = foot_z + (pose.joints[ji].z - foot_z)
                                    * AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
            }

            for (int k = 0; k < 5; ++k) pose.joints[ground_joints[k]].z = saved[k];
        }

        // ---- Procedural cage mesh (emitted first so debug shapes overdraw) ----
        if (!mesh.vertices.empty()) {
            float foot_z = t.z;
            float scale  = AnimationConfig::ISO_CHAR_HEIGHT_SCALE;
            for (const BasaltVertex &sv : mesh.vertices) {
                BasaltVertex v = sv;
                v.pos_z = foot_z + (v.pos_z - foot_z) * scale;
                v.nz *= scale;
                float nlen = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
                if (nlen > 1e-6f) { v.nx /= nlen; v.ny /= nlen; v.nz /= nlen; }
                verts.push_back(v);
            }
        }

        using J = Joint;
        auto j = [&](J jt) -> const glm::vec3 & { return pose.joints[(int)jt]; };

        // ---- Foundation: World Root ground marker + parenting tether to Hips ----
        {
            glm::vec3 trace_color(0.3f, 0.85f, 0.85f);  // teal