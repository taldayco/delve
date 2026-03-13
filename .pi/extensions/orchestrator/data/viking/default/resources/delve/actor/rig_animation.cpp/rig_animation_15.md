// --- D. Clavicles ---
                auto compute_clavicle = [&](Joint clav, Joint upper_arm,
                                            const glm::vec3 &ref) {
                    glm::vec3 bone_dir = jt[(int)upper_arm] - jt[(int)clav];
                    float blen = glm::length(bone_dir);
                    if (blen < 1e-5f) {
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)clav], 1.0f);
                        xforms.bones[(int)clav] = m;
                    } else {
                        glm::vec3 right, fwd, up;
                        build_bone_basis(bone_dir, ref, right, fwd, up);
                        xforms.bones[(int)clav] = make_bone_mat4(right, fwd, up, jt[(int)clav]);
                    }
                };
                compute_clavicle(J::L_CLAVICLE, J::L_UPPER_ARM, chest_fwd);
                compute_clavicle(J::R_CLAVICLE, J::R_UPPER_ARM, -chest_fwd);

                // --- E. IK Virtual Joints (identity rotation at position) ---
                static constexpr Joint ik_joints[] = {
                    J::POLE_KNEE_L, J::POLE_KNEE_R,
                    J::POLE_ELBOW_L, J::POLE_ELBOW_R,
                    J::IK_FOOT_L, J::IK_FOOT_R,
                    J::IK_HAND_L, J::IK_HAND_R,
                };
                for (Joint j : ik_joints) {
                    glm::mat4 m(1.0f);
                    m[3] = glm::vec4(jt[(int)j], 1.0f);
                    xforms.bones[(int)j] = m;
                }

                (void)cfg;
                (void)chest_rght;
            });
        });

    // =========================================================================
    // 7.75 MeshGenerationSystem
    //      Generates procedural bone cage meshes from RigTransforms + ActorConfig.
    //      Runs AFTER RigTransformSystem, BEFORE AnimationLogSystem.
    // =========================================================================
    ecs.system("MeshGenerationSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([](ActorTag,
                        const ActorConfig    &cfg,
                        const RigPose        &pose,
                        const RigTransforms  &xforms,
                        ProceduralMesh       &mesh) {
                using J = Joint;
                mesh.vertices.clear();
                mesh.vertices.reserve(2048);

                auto &v = mesh.vertices;
                const auto &b = xforms.bones;
                const auto &jt = pose.joints;

                glm::vec4 body(0.55f, 0.52f, 0.48f, 0.1f);  // warm grey, low sheen

                // --- SPINE CHAIN (4 segments, 6-sided) ---
                append_bone_cage(v, b[(int)J::HIPS],     jt[(int)J::SPINE_01],
                                 cfg.torso_radius * 1.3f, cfg.torso_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::SPINE_01], jt[(int)J::SPINE_02],
                                 cfg.torso_radius * 1.0f, cfg.torso_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::SPINE_02], jt[(int)J::CHEST],
                                 cfg.torso_radius * 1.0f, cfg.torso_radius * 1.4f, 6, body);
                append_bone_cage(v, b[(int)J::CHEST],    jt[(int)J::NECK],
                                 cfg.torso_radius * 1.4f, cfg.neck_radius,         6, body);

                // --- HEAD CHAIN (2 segments, 6-sided) ---
                append_bone_cage(v, b[(int)J::NECK],     jt[(int)J::HEAD],
                                 cfg.head_radius * 0.4f,  cfg.head_radius * 0.6f, 6, body);
                append_bone_cage(v, b[(int)J::HEAD],     jt[(int)J::HEAD_END],
                                 cfg.head_radius * 0.6f,  cfg.head_radius * 0.3f, 6, body);

                // --- LEFT LEG (4 segments, 6-sided) ---
                append_bone_cage(v, b[(int)J::HIPS],          jt[(int)J::L_UPPER_LEG],
                                 cfg.leg_radius * 1.4f,       cfg.leg_radius * 1.4f, 6, body);
                append_bone_cage(v, b[(int)J::L_UPPER_LEG],   jt[(int)J::L_LOWER_LEG],
                                 cfg.leg_radius * 1.4f,       cfg.leg_radius * 0.9f, 6, body);
                append_bone_cage(v, b[(int)J::L_LOWER_LEG],   jt[(int)J::L_FOOT],
                                 cfg.leg_radius * 0.9f,       cfg.leg_radius * 0.8f, 6, body);
                append_bone_cage(v, b[(int)J::L_FOOT],        jt[(int)J::L_TOE],
                                 cfg.leg_radius * 0.6f,       cfg.leg_radius * 0.3f, 6, body);

                // --- RIGHT LEG (4 segments, 6-sided) ---
                append_bone_cage(v, b[(int)J::HIPS],          jt[(int)J::R_UPPER_LEG],
                                 cfg.leg_radius * 1.4f,       cfg.leg_radius * 1.4f, 6, body);
                append_bone_cage(v, b[(int)J::R_UPPER_LEG],   jt[(int)J::R_LOWER_LEG],
                                 cfg.leg_radius * 1.4f,       cfg.leg_radius * 0.9f, 6, body);
                append_bone_cage(v, b[(int)J::R_LOWER_LEG],   jt[(int)J::R_FOOT],
                                 cfg.leg_radius * 0.9f,       cfg.leg_radius * 0.8f, 6, body);
                append_bone_cage(v, b[(int)J::R_FOOT],        jt[(int)J::R_TOE],
                                 cfg.leg_radius * 0.6f,       cfg.leg_radius * 0.3f, 6, body);