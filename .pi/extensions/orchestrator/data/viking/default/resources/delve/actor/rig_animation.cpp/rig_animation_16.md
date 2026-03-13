// --- LEFT ARM (4 segments, 6-sided) ---
                append_bone_cage(v, b[(int)J::CHEST],         jt[(int)J::L_CLAVICLE],
                                 cfg.arm_radius * 1.5f,       cfg.arm_radius * 1.5f, 6, body);
                append_bone_cage(v, b[(int)J::L_CLAVICLE],    jt[(int)J::L_UPPER_ARM],
                                 cfg.arm_radius * 1.5f,       cfg.arm_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::L_UPPER_ARM],   jt[(int)J::L_LOWER_ARM],
                                 cfg.arm_radius * 1.0f,       cfg.arm_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::L_LOWER_ARM],   jt[(int)J::L_HAND],
                                 cfg.arm_radius * 1.0f,       cfg.arm_radius * 0.75f, 6, body);

                // --- RIGHT ARM (4 segments, 6-sided) ---
                append_bone_cage(v, b[(int)J::CHEST],         jt[(int)J::R_CLAVICLE],
                                 cfg.arm_radius * 1.5f,       cfg.arm_radius * 1.5f, 6, body);
                append_bone_cage(v, b[(int)J::R_CLAVICLE],    jt[(int)J::R_UPPER_ARM],
                                 cfg.arm_radius * 1.5f,       cfg.arm_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::R_UPPER_ARM],   jt[(int)J::R_LOWER_ARM],
                                 cfg.arm_radius * 1.0f,       cfg.arm_radius * 1.0f, 6, body);
                append_bone_cage(v, b[(int)J::R_LOWER_ARM],   jt[(int)J::R_HAND],
                                 cfg.arm_radius * 1.0f,       cfg.arm_radius * 0.75f, 6, body);
            });
        });

    // =========================================================================
    // 9. AnimationLogSystem
    //    JSONL frame telemetry — runs after MeshGenerationSystem.
    // =========================================================================
    ecs.system("AnimationLogSystem")
        .kind(flecs::PostUpdate)
        .run([&anim_log, &ecs, &camera, player_entity](flecs::iter &) {
            if (!anim_log.active) return;
            if (!player_entity.is_alive()) return;

            const auto *t    = player_entity.get<Transform>();
            const auto *vel  = player_entity.get<Velocity>();
            const auto *gait = player_entity.get<ProceduralGait>();
            const auto *legs = player_entity.get<LegState>();
            const auto *pose = player_entity.get<RigPose>();
            const auto *cfg  = player_entity.get<ActorConfig>();
            const auto *anim = player_entity.get<RigState>();
            if (!t || !vel || !gait || !legs || !pose || !cfg || !anim) return;

            float dt = ecs.delta_time();
            anim_log.begin_frame(dt);
            anim_log.log_transform(*t, *vel);
            anim_log.log_gait(*gait);
            anim_log.log_legs(*legs, *t, *cfg);
            anim_log.log_joints(*pose, *t);
            anim_log.log_finalize(anim->support_balance,
                                   anim->lean_x, anim->lean_y);
            anim_log.log_camera(camera);
            anim_log.log_dynamics(*anim, *vel, dt);
            anim_log.log_arm_swing(*anim);
            anim_log.log_grounding(*anim, *legs);
            anim_log.end_frame();
        });
}