#pragma once
#include "rig.h"
#include "camera/camera.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <glm/glm.hpp>
#include <SDL3/SDL_log.h>

class AnimationLogger {
public:
    bool active = false;

    void toggle() {
        if (active) {
            if (file) { fclose(file); file = nullptr; }
            active = false;
            SDL_Log("Animation logging stopped");
        } else {
            file = fopen("animation_log.jsonl", "w");
            if (file) {
                active = true;
                frame_count = 0;
                SDL_Log("Animation logging started: animation_log.jsonl");
            } else {
                SDL_Log("Animation logging: failed to open file");
            }
        }
    }

    void close() {
        if (file) { fclose(file); file = nullptr; }
        active = false;
    }

    void begin_frame(float dt) {
        if (!active || !file) return;
        fprintf(file, "{\"frame\":%llu,\"dt\":%.6f", (unsigned long long)frame_count, dt);
    }

    void log_transform(const Transform &t, const Velocity &vel) {
        if (!active || !file) return;
        float speed = sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
        fprintf(file,
            ",\"transform\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f,\"facing\":%.4f}"
            ",\"velocity\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f,\"speed\":%.4f}",
            t.x, t.y, t.z, t.facing,
            vel.x, vel.y, vel.z, speed);
    }

    void log_gait(const ProceduralGait &g) {
        if (!active || !file) return;
        fprintf(file,
            ",\"gait\":{\"phase\":%.4f,\"stride_len\":%.4f,\"step_height\":%.4f,\"step_duration\":%.4f}",
            g.phase, g.stride_len, g.step_height, g.step_duration);
    }

    void log_legs(const LegState &legs, const Transform &t, const ActorConfig &cfg) {
        if (!active || !file) return;

        float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
        float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);
        float hip_sign[2] = { -1.0f, 1.0f };
        float chain_len = cfg.leg_len + cfg.shin_len;

        bool both_stepping = legs.stepping[0] && legs.stepping[1];

        fprintf(file, ",\"legs\":{\"both_stepping\":%s,", both_stepping ? "true" : "false");

        const char *names[2] = {"left", "right"};
        for (int i = 0; i < 2; ++i) {
            float hx = t.x + rght_x * hip_sign[i] * cfg.hip_width;
            float hy = t.y + rght_y * hip_sign[i] * cfg.hip_width;

            float dx = legs.foot[i].x - hx;
            float dy = legs.foot[i].y - hy;
            float foot_along_fwd = dx * fwd_x + dy * fwd_y;
            float foot_lateral   = dx * rght_x + dy * rght_y;
            float foot_vertical  = legs.foot[i].z - t.z;

            float dz = legs.foot[i].z - t.z;
            float reach = sqrtf(dx*dx + dy*dy + dz*dz);
            float reach_ratio = reach / chain_len;

            if (i > 0) fprintf(file, ",");
            fprintf(file,
                "\"%s\":{"
                "\"foot\":[%.4f,%.4f,%.4f],"
                "\"target\":[%.4f,%.4f,%.4f],"
                "\"prev_foot\":[%.4f,%.4f,%.4f],"
                "\"stepping\":%s,\"progress\":%.4f,"
                "\"foot_along_fwd\":%.4f,"
                "\"foot_lateral\":%.4f,"
                "\"foot_vertical\":%.4f,"
                "\"reach\":%.4f,"
                "\"reach_ratio\":%.4f,"
                "\"clamped\":%s}",
                names[i],
                legs.foot[i].x, legs.foot[i].y, legs.foot[i].z,
                legs.target[i].x, legs.target[i].y, legs.target[i].z,
                legs.prev_foot[i].x, legs.prev_foot[i].y, legs.prev_foot[i].z,
                legs.stepping[i] ? "true" : "false",
                legs.progress[i],
                foot_along_fwd,
                foot_lateral,
                foot_vertical,
                reach, reach_ratio,
                (reach > chain_len) ? "true" : "false");
        }
        fprintf(file, "}");
    }

    void log_joints(const RigPose &pose, const Transform &t) {
        if (!active || !file) return;
        static const char *joint_names[(int)Joint::COUNT] = {
            "ROOT", "HIPS", "SPINE_01", "SPINE_02", "CHEST", "NECK", "HEAD", "HEAD_END",
            "L_CLAVICLE", "L_UPPER_ARM", "L_LOWER_ARM", "L_HAND",
            "R_CLAVICLE", "R_UPPER_ARM", "R_LOWER_ARM", "R_HAND",
            "L_UPPER_LEG", "L_LOWER_LEG", "L_FOOT", "L_TOE",
            "R_UPPER_LEG", "R_LOWER_LEG", "R_FOOT", "R_TOE",
            "POLE_KNEE_L", "POLE_KNEE_R", "POLE_ELBOW_L", "POLE_ELBOW_R",
            "IK_FOOT_L", "IK_FOOT_R", "IK_HAND_L", "IK_HAND_R"
        };

        fprintf(file, ",\"joints\":{");
        for (int i = 0; i < (int)Joint::COUNT; ++i) {
            if (i > 0) fprintf(file, ",");
            const auto &j = pose.joints[i];
            fprintf(file, "\"%s\":[%.4f,%.4f,%.4f]", joint_names[i], j.x, j.y, j.z);
        }
        fprintf(file, "}");

        using J = Joint;
        auto rel = [&](Joint child, Joint parent) {
            auto &c = pose.joints[(int)child];
            auto &p = pose.joints[(int)parent];
            return glm::vec3(c.x - p.x, c.y - p.y, c.z - p.z);
        };

        float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);

        auto l_elbow_off = rel(J::L_LOWER_ARM, J::L_UPPER_ARM);
        auto r_elbow_off = rel(J::R_LOWER_ARM, J::R_UPPER_ARM);
        float l_arm_swing = l_elbow_off.x * fwd_x + l_elbow_off.y * fwd_y;
        float r_arm_swing = r_elbow_off.x * fwd_x + r_elbow_off.y * fwd_y;

        auto l_shoulder_off = rel(J::L_UPPER_ARM, J::CHEST);
        auto r_shoulder_off = rel(J::R_UPPER_ARM, J::CHEST);

        float root_offset_x = pose.joints[(int)J::HIPS].x - t.x;
        float root_offset_y = pose.joints[(int)J::HIPS].y - t.y;

        fprintf(file,
            ",\"arm_diagnostics\":{"
            "\"l_elbow_rel_shoulder\":[%.4f,%.4f,%.4f],"
            "\"r_elbow_rel_shoulder\":[%.4f,%.4f,%.4f],"
            "\"l_arm_swing_fwd\":%.4f,"
            "\"r_arm_swing_fwd\":%.4f,"
            "\"l_shoulder_rel_chest\":[%.4f,%.4f,%.4f],"
            "\"r_shoulder_rel_chest\":[%.4f,%.4f,%.4f],"
            "\"arms_identical\":%s}",
            l_elbow_off.x, l_elbow_off.y, l_elbow_off.z,
            r_elbow_off.x, r_elbow_off.y, r_elbow_off.z,
            l_arm_swing, r_arm_swing,
            l_shoulder_off.x, l_shoulder_off.y, l_shoulder_off.z,
            r_shoulder_off.x, r_shoulder_off.y, r_shoulder_off.z,
            (l_arm_swing == r_arm_swing) ? "true" : "false");

        auto spine_off = rel(J::SPINE_01, J::HIPS);
        auto chest_off = rel(J::CHEST, J::SPINE_01);
        auto neck_off  = rel(J::NECK, J::CHEST);
        fprintf(file,
            ",\"spine_diagnostics\":{"
            "\"root_sway_offset\":[%.4f,%.4f],"
            "\"spine_rel_root\":[%.4f,%.4f,%.4f],"
            "\"chest_rel_spine\":[%.4f,%.4f,%.4f],"
            "\"neck_rel_chest\":[%.4f,%.4f,%.4f]}",
            root_offset_x, root_offset_y,
            spine_off.x, spine_off.y, spine_off.z,
            chest_off.x, chest_off.y, chest_off.z,
            neck_off.x, neck_off.y, neck_off.z);
    }

    void log_camera(const CameraState &cam) {
        if (!active || !file) return;
        fprintf(file,
            ",\"camera\":{\"world_x\":%.4f,\"world_y\":%.4f,\"zoom\":%.4f,\"following\":%s}",
            cam.world_x, cam.world_y, cam.zoom, cam.following ? "true" : "false");
    }

    void log_finalize(float sway_phase, float sway_amount, float lean_x, float lean_y) {
        if (!active || !file) return;
        float sway_displacement = sinf(sway_phase) * sway_amount;
        fprintf(file,
            ",\"finalize\":{"
            "\"sway_phase\":%.4f,"
            "\"sway_amount\":%.4f,"
            "\"sway_displacement\":%.4f,"
            "\"lean_x\":%.4f,\"lean_y\":%.4f,"
            "\"lean_magnitude\":%.4f}",
            sway_phase, sway_amount, sway_displacement,
            lean_x, lean_y,
            sqrtf(lean_x * lean_x + lean_y * lean_y));
    }

    void end_frame() {
        if (!active || !file) return;
        fprintf(file, "}\n");
        fflush(file);
        frame_count++;
    }

    void log_dynamics(const RigState &anim, const Velocity &vel, float dt) {
        if (!active || !file) return;
        glm::vec3 cur_vel(vel.x, vel.y, 0.0f);
        glm::vec3 accel = (dt > 1e-6f) ? ((cur_vel - prev_velocity_log) / dt)
                                       : glm::vec3(0.0f);
        float jerk = (dt > 1e-6f) ? glm::length(accel - prev_accel_log) / dt : 0.0f;
        prev_accel_log    = accel;
        prev_velocity_log = cur_vel;

        float com_offset = glm::length(glm::vec3(anim.smooth_velocity.x,
                                                   anim.smooth_velocity.y, 0.0f)
                                       - cur_vel);
        fprintf(file,
            ",\"dynamics\":{"
            "\"jerk\":%.4f,\"com_offset\":%.4f,"
            "\"lean_x\":%.4f,\"lean_y\":%.4f,"
            "\"smooth_vel\":[%.4f,%.4f]}",
            jerk, com_offset,
            anim.lean_x, anim.lean_y,
            anim.smooth_velocity.x, anim.smooth_velocity.y);
    }

    void log_arm_swing(const RigState &anim) {
        if (!active || !file) return;
        float l_shoulder_lag = fabsf(anim.l_arm_target - anim.l_shoulder_smooth);
        float l_elbow_lag    = fabsf(anim.l_shoulder_smooth - anim.l_elbow_smooth);
        float l_wrist_lag    = fabsf(anim.l_elbow_smooth - anim.l_wrist_smooth);
        fprintf(file,
            ",\"arm_swing\":{"
            "\"l_target\":%.4f,\"r_target\":%.4f,"
            "\"l_shoulder_lag\":%.4f,\"l_elbow_lag\":%.4f,\"l_wrist_lag\":%.4f,"
            "\"r_shoulder_lag\":%.4f,\"r_elbow_lag\":%.4f,\"r_wrist_lag\":%.4f}",
            anim.l_arm_target, anim.r_arm_target,
            l_shoulder_lag, l_elbow_lag, l_wrist_lag,
            fabsf(anim.r_arm_target - anim.r_shoulder_smooth),
            fabsf(anim.r_shoulder_smooth - anim.r_elbow_smooth),
            fabsf(anim.r_elbow_smooth - anim.r_wrist_smooth));
    }

    void log_grounding(const RigState &anim, const LegState &legs) {
        if (!active || !file) return;
        fprintf(file,
            ",\"grounding\":{"
            "\"both_stepping\":%s,"
            "\"l_contact_vel\":%.4f,\"r_contact_vel\":%.4f,"
            "\"l_stepping\":%s,\"r_stepping\":%s}",
            (legs.stepping[0] && legs.stepping[1]) ? "true" : "false",
            anim.foot_contact_velocity[0], anim.foot_contact_velocity[1],
            legs.stepping[0] ? "true" : "false",
            legs.stepping[1] ? "true" : "false");
    }

private:
    FILE *file = nullptr;
    uint64_t frame_count = 0;
    glm::vec3 prev_velocity_log{0.0f};
    glm::vec3 prev_accel_log{0.0f};
};
