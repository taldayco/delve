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