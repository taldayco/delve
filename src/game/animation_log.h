#pragma once
#include "actor.h"
#include "camera/camera.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
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
            // Compute hip socket position
            float hx = t.x + rght_x * hip_sign[i] * cfg.hip_width;
            float hy = t.y + rght_y * hip_sign[i] * cfg.hip_width;
            float hz = t.z;

            // Foot position relative to hip, projected onto facing direction
            float dx = legs.foot[i].x - hx;
            float dy = legs.foot[i].y - hy;
            float foot_along_fwd = dx * fwd_x + dy * fwd_y;
            float foot_lateral   = dx * rght_x + dy * rght_y;
            float foot_vertical  = legs.foot[i].z - hz;

            // IK reach
            float dz = legs.foot[i].z - hz;
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

    void log_joints(const SkeletonPose &pose, const Transform &t) {
        if (!active || !file) return;
        static const char *joint_names[(int)Joint::COUNT] = {
            "ROOT", "SPINE", "CHEST", "NECK", "HEAD",
            "L_SHOULDER", "L_ELBOW", "L_WRIST",
            "R_SHOULDER", "R_ELBOW", "R_WRIST",
            "L_HIP", "L_KNEE", "L_ANKLE",
            "R_HIP", "R_KNEE", "R_ANKLE"
        };

        // Raw joint positions
        fprintf(file, ",\"joints\":{");
        for (int i = 0; i < (int)Joint::COUNT; ++i) {
            if (i > 0) fprintf(file, ",");
            const auto &j = pose.joints[i];
            fprintf(file, "\"%s\":[%.4f,%.4f,%.4f]", joint_names[i], j.x, j.y, j.z);
        }
        fprintf(file, "}");

        // Derived arm diagnostics — offsets relative to their parent joint
        using J = Joint;
        auto rel = [&](Joint child, Joint parent) {
            auto &c = pose.joints[(int)child];
            auto &p = pose.joints[(int)parent];
            return glm::vec3(c.x - p.x, c.y - p.y, c.z - p.z);
        };

        // Arm swing: project elbow offset from shoulder onto facing forward axis
        float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);

        auto l_elbow_off = rel(J::L_ELBOW, J::L_SHOULDER);
        auto r_elbow_off = rel(J::R_ELBOW, J::R_SHOULDER);
        float l_arm_swing = l_elbow_off.x * fwd_x + l_elbow_off.y * fwd_y;
        float r_arm_swing = r_elbow_off.x * fwd_x + r_elbow_off.y * fwd_y;

        // Shoulder relative to chest
        auto l_shoulder_off = rel(J::L_SHOULDER, J::CHEST);
        auto r_shoulder_off = rel(J::R_SHOULDER, J::CHEST);

        // Root-to-transform offset (actual sway displacement)
        float root_offset_x = pose.joints[(int)J::ROOT].x - t.x;
        float root_offset_y = pose.joints[(int)J::ROOT].y - t.y;

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

        // Spine diagnostics — is the spine a rigid column?
        auto spine_off = rel(J::SPINE, J::ROOT);
        auto chest_off = rel(J::CHEST, J::SPINE);
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

    void log_camera(const CameraState &cam) {
        if (!active || !file) return;
        fprintf(file,
            ",\"camera\":{\"world_x\":%.4f,\"world_y\":%.4f,\"zoom\":%.4f,\"following\":%s}",
            cam.world_x, cam.world_y, cam.zoom, cam.following ? "true" : "false");
    }

    void log_dynamics(glm::vec2 smooth_vel, float lean_x, float lean_y) {
        if (!active || !file) return;
        fprintf(file,
            ",\"dynamics\":{\"smooth_vel\":[%.4f,%.4f],\"lean\":[%.4f,%.4f]}",
            smooth_vel.x, smooth_vel.y, lean_x, lean_y);
    }

    void log_arm_swing(float phase_l, float phase_r, float delay_l, float delay_r) {
        if (!active || !file) return;
        fprintf(file,
            ",\"arm_swing\":{\"phase_l\":%.4f,\"phase_r\":%.4f,\"delay_l\":%.4f,\"delay_r\":%.4f}",
            phase_l, phase_r, delay_l, delay_r);
    }

    void log_grounding(const LegState &legs, float step_duration) {
        if (!active || !file) return;
        fprintf(file,
            ",\"grounding\":{\"stepping_l\":%s,\"stepping_r\":%s,\"step_duration\":%.4f}",
            legs.stepping[0] ? "true" : "false",
            legs.stepping[1] ? "true" : "false",
            step_duration);
    }

    void end_frame() {
        if (!active || !file) return;
        fprintf(file, "}\n");
        fflush(file);
        frame_count++;
    }

private:
    FILE *file = nullptr;
    uint64_t frame_count = 0;
};
