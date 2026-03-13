// Toes: extend forward from foot
                glm::vec3 facing_dir(fwd_x, fwd_y, 0.0f);
                pose.joints[(int)J::L_TOE] = pose.joints[(int)J::L_FOOT]
                    + facing_dir * cfg.toe_len;
                pose.joints[(int)J::R_TOE] = pose.joints[(int)J::R_FOOT]
                    + facing_dir * cfg.toe_len;

                // Pole targets
                glm::vec3 fwd3(fwd_x, fwd_y, 0.0f);
                pose.joints[(int)J::POLE_KNEE_L]  = pose.joints[(int)J::L_LOWER_LEG] + fwd3 * 0.25f;
                pose.joints[(int)J::POLE_KNEE_R]  = pose.joints[(int)J::R_LOWER_LEG] + fwd3 * 0.25f;

                float cf_x = cosf(anim.chest_facing), cf_y = sinf(anim.chest_facing);
                glm::vec3 chest_fwd3(cf_x, cf_y, 0.0f);
                pose.joints[(int)J::POLE_ELBOW_L] = pose.joints[(int)J::L_LOWER_ARM] - chest_fwd3 * 0.25f;
                pose.joints[(int)J::POLE_ELBOW_R] = pose.joints[(int)J::R_LOWER_ARM] - chest_fwd3 * 0.25f;

                // IK goals = current end-effector positions
                pose.joints[(int)J::IK_FOOT_L] = pose.joints[(int)J::L_FOOT];
                pose.joints[(int)J::IK_FOOT_R] = pose.joints[(int)J::R_FOOT];
                pose.joints[(int)J::IK_HAND_L] = pose.joints[(int)J::L_HAND];
                pose.joints[(int)J::IK_HAND_R] = pose.joints[(int)J::R_HAND];

                (void)cfg;
            });
        });

    // =========================================================================
    // 7.5 RigTransformSystem
    //     Computes per-joint glm::mat4 transforms from finalized joint positions.
    //     Runs AFTER SkeletonFinaliseSystem, BEFORE AnimationLogSystem.
    // =========================================================================
    ecs.system("RigTransformSystem")
        .kind(flecs::PostUpdate)
        .run([&ecs](flecs::iter &) {
            ecs.each([](ActorTag,
                        const ActorConfig    &cfg,
                        const RigState       &anim,
                        const RigPose        &pose,
                        RigTransforms        &xforms) {
                using J = Joint;
                const auto &jt = pose.joints;

                float vf = anim.visual_facing;
                glm::vec3 vis_fwd(cosf(vf), sinf(vf), 0.0f);
                glm::vec3 vis_rght(-sinf(vf), cosf(vf), 0.0f);

                float cf = anim.chest_facing;
                glm::vec3 chest_fwd(cosf(cf), sinf(cf), 0.0f);
                glm::vec3 chest_rght(-sinf(cf), cosf(cf), 0.0f);

                // --- A. ROOT ---
                xforms.bones[(int)J::ROOT] = make_bone_mat4(
                    vis_rght, vis_fwd, glm::vec3(0.0f, 0.0f, 1.0f),
                    jt[(int)J::ROOT]);

                // --- B. Spine chain (table-driven with chest_blend) ---
                struct SpineEntry { Joint self; Joint child; float chest_blend; };
                static constexpr SpineEntry spine_chain[] = {
                    { J::HIPS,     J::SPINE_01, 0.0f },
                    { J::SPINE_01, J::SPINE_02, 0.0f },
                    { J::SPINE_02, J::CHEST,    0.5f },
                    { J::CHEST,    J::NECK,     1.0f },
                    { J::NECK,     J::HEAD,     1.0f },
                };
                for (const auto &e : spine_chain) {
                    glm::vec3 bone_dir = jt[(int)e.child] - jt[(int)e.self];
                    float blen = glm::length(bone_dir);
                    if (blen < 1e-5f) {
                        // Degenerate — identity rotation at position
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)e.self], 1.0f);
                        xforms.bones[(int)e.self] = m;
                        continue;
                    }
                    glm::vec3 ref = glm::mix(vis_fwd, chest_fwd, e.chest_blend);
                    float ref_len = glm::length(ref);
                    if (ref_len < 1e-5f) ref = vis_fwd; // opposing facings fallback
                    else ref /= ref_len;

                    glm::vec3 right, fwd, up;
                    build_bone_basis(bone_dir, ref, right, fwd, up);
                    xforms.bones[(int)e.self] = make_bone_mat4(right, fwd, up, jt[(int)e.self]);
                }
                // HEAD: point toward HEAD_END
                {
                    glm::vec3 bone_dir = jt[(int)J::HEAD_END] - jt[(int)J::HEAD];
                    float blen = glm::length(bone_dir);
                    if (blen < 1e-5f) {
                        glm::mat4 m(1.0f);
                        m[3] = glm::vec4(jt[(int)J::HEAD], 1.0f);
                        xforms.bones[(int)J::HEAD] = m;
                    } else {
                        glm::vec3 right, fwd, up;
                        build_bone_basis(bone_dir, chest_fwd, right, fwd, up);
                        xforms.bones[(int)J::HEAD] = make_bone_mat4(right, fwd, up, jt[(int)J::HEAD]);
                    }
                }
                // HEAD_END: copy HEAD rotation, override position
                {
                    glm::mat4 m = xforms.bones[(int)J::HEAD];
                    m[3] = glm::vec4(jt[(int)J::HEAD_END], 1.0f);
                    xforms.bones[(int)J::HEAD_END] = m;
                }