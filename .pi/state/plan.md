I'll analyze the codebase to understand the current actor/animation system before planning.

<tool_call>
{"name": "mcp", "arguments": {"tool": "project_overview", "args": "{}"}}
</tool_call>
<tool_response>
Project: delve
Language: C++20
Build: CMake 3.20+
Key directories:
  src/engine/: app.cpp, app.h, camera.cpp, camera.h, gpu_context.cpp, gpu_context.h, input.cpp, input.h, render_context.cpp, render_context.h, ui.cpp, ui.h
  src/game/: actor.h, config.h, game_state.h, topo_game.cpp, topo_game.h
  src/game/render/: actor_renderer.cpp, actor_renderer.h
  src/game/terrain/: basalt.cpp, basalt.h, color.cpp, color.h, contour.cpp, contour.h, delve_render.cpp, delve_render.h, detail.cpp, detail.h, flood_fill.cpp, flood_fill.h, hex.cpp, hex.h, isometric.cpp, isometric.h, lava.cpp, lava.h, map_data.h, map_gen.cpp, map_gen.h, map_util.cpp, map_util.h, noise_cache.h, noise_composer.cpp, noise_composer.h, noise.cpp, noise.h, noise_layers.cpp, noise_layers.h, palettes.cpp, palettes.h, terrain_generator.cpp, terrain_generator.h, terrain_mesh.cpp, terrain_mesh.h, terrain_renderer.cpp, terrain_renderer.h, util.h
  src/test/: main.cpp, test_harness.h, tests.cpp
  src/shaders/: terrain.frag, terrain.vert, actor.frag, actor.vert
</tool_response>

<tool_call>
{"name": "mcp", "arguments": {"tool": "code_find_definition", "args": "{\"symbol\": \"actor\", \"file\": \"src/game/actor.h\"}"}}
</tool_call>
<tool_response>
File: src/game/actor.h
```cpp
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <array>
#include <cmath>

// ── Skeleton ──────────────────────────────────────────────────────────────────

enum class JointID : uint8_t {
    Root = 0,
    Hips,
    Spine,
    Chest,
    Neck,
    Head,
    ShoulderL, UpperArmL, LowerArmL, HandL,
    ShoulderR, UpperArmR, LowerArmR, HandR,
    HipL, ThighL, ShinL, FootL,
    HipR, ThighR, ShinR, FootR,
    COUNT
};
constexpr int JOINT_COUNT = (int)JointID::COUNT;

// Bind-pose lengths (in world units)
struct BoneLengths {
    float torso  = 12.0f;
    float neck   =  3.0f;
    float head   =  5.0f;
    float upper_arm = 7.0f;
    float lower_arm = 6.0f;
    float hand   =  2.0f;
    float thigh  = 10.0f;
    float shin   =  9.0f;
    float foot   =  3.0f;
};

struct Joint {
    glm::quat local_rot  = glm::quat(1,0,0,0);  // local rotation from bind pose
    glm::vec3 world_pos  = glm::vec3(0);
    glm::vec3 bind_dir   = glm::vec3(0,-1,0);   // direction this bone points in bind
    float     length     = 1.0f;
    int       parent     = -1;
};

struct Skeleton {
    std::array<Joint, JOINT_COUNT> joints;
    BoneLengths lengths;
};

// ── Pose / Animation ──────────────────────────────────────────────────────────

struct Pose {
    // Per-joint Euler angles (degrees) for animation blending
    std::array<glm::vec3, JOINT_COUNT> angles = {};
};

// Simple keyframe clip
struct AnimClip {
    std::string name;
    float       duration   = 1.0f;   // seconds
    float       time       = 0.0f;   // playback cursor
    std::vector<std::pair<float, Pose>> keys; // (timestamp, pose)
};

// ── Actor ─────────────────────────────────────────────────────────────────────

struct Actor {
    glm::vec3  position    = glm::vec3(0);
    glm::quat  facing      = glm::quat(1,0,0,0);
    float      move_speed  = 80.0f;
    float      anim_time   = 0.0f;

    Skeleton   skeleton;
    AnimClip   walk_clip;
    AnimClip   idle_clip;
    AnimClip*  current_clip = nullptr;

    bool       is_moving   = false;
};

// ── Forward kinematics ────────────────────────────────────────────────────────

inline glm::quat euler_to_quat(glm::vec3 deg) {
    return glm::quat(glm::radians(deg));
}

// Build skeleton from BoneLengths – sets parent, bind_dir, length for each joint
inline void build_skeleton(Skeleton& sk) {
    auto& j = sk.joints;
    auto& L = sk.lengths;

    // Helper: set joint fields
    auto set = [&](JointID id, JointID par, glm::vec3 dir, float len) {
        int i = (int)id;
        j[i].parent   = (int)par;
        j[i].bind_dir = glm::normalize(dir);
        j[i].length   = len;
        j[i].local_rot = glm::quat(1,0,0,0);
    };

    // Root → Hips
    j[(int)JointID::Root].parent = -1;
    j[(int)JointID::Root].length = 0;

    set(JointID::Hips,   JointID::Root,  {0, 1,0}, L.torso * 0.15f);
    set(JointID::Spine,  JointID::Hips,  {0, 1,0}, L.torso * 0.35f);
    set(JointID::Chest,  JointID::Spine, {0, 1,0}, L.torso * 0.35f);
    set(JointID::Neck,   JointID::Chest, {0, 1,0}, L.neck);
    set(JointID::Head,   JointID::Neck,  {0, 1,0}, L.head);

    // Left arm
    set(JointID::ShoulderL, JointID::Chest, {-1,0,0}, 1.5f);
    set(JointID::UpperArmL, JointID::ShoulderL, {-1,0,0}, L.upper_arm);
    set(JointID::LowerArmL, JointID::UpperArmL, {-1,0,0}, L.lower_arm);
    set(JointID::HandL,     JointID::LowerArmL, {-1,0,0}, L.hand);

    // Right arm
    set(JointID::ShoulderR, JointID::Chest, {1,0,0}, 1.5f);
    set(JointID::UpperArmR, JointID::ShoulderR, {1,0,0}, L.upper_arm);
    set(JointID::LowerArmR, JointID::UpperArmR, {1,0,0}, L.lower_arm);
    set(JointID::HandR,     JointID::LowerArmR, {1,0,0}, L.hand);

    // Left leg
    set(JointID::HipL,   JointID::Hips,  {-0.2f,-1,0}, 0.5f);
    set(JointID::ThighL, JointID::HipL,  {0,-1,0}, L.thigh);
    set(JointID::ShinL,  JointID::ThighL,{0,-1,0}, L.shin);
    set(JointID::FootL,  JointID::ShinL, {0,-0.3f,1}, L.foot);

    // Right leg
    set(JointID::HipR,   JointID::Hips,  {0.2f,-1,0}, 0.5f);
    set(JointID::ThighR, JointID::HipR,  {0,-1,0}, L.thigh);
    set(JointID::ShinR,  JointID::ThighR,{0,-1,0}, L.shin);
    set(JointID::FootR,  JointID::ShinR, {0,-0.3f,1}, L.foot);
}

// Forward kinematics: compute world_pos for every joint
inline void fk_update(Skeleton& sk) {
    auto& j = sk.joints;

    // Root is at actor position – caller sets j[0].world_pos
    for (int i = 1; i < JOINT_COUNT; ++i) {
        int p = j[i].parent;
        glm::quat prot = (p == 0) ? j[p].local_rot
                                   : j[p].local_rot;  // accumulated below
        // Accumulate parent world rotation
        glm::quat world_rot = glm::quat(1,0,0,0);
        int cur = i;
        while (cur != -1) {
            world_rot = j[cur].local_rot * world_rot;
            cur = j[cur].parent;
        }
        // Parent world pos + rotated bind direction * length
        glm::quat parent_world = glm::quat(1,0,0,0);
        int pcur = p;
        while (pcur != -1) {
            parent_world = j[pcur].local_rot * parent_world;
            pcur = j[pcur].parent;
        }
        j[i].world_pos = j[p].world_pos
                       + glm::mat3_cast(parent_world) * j[i].bind_dir * j[i].length;
    }
}

// ── Walk cycle procedural animation ──────────────────────────────────────────

inline void apply_walk_pose(Skeleton& sk, float t, float speed_scale = 1.0f) {
    // t = phase [0, 2π)
    auto& j = sk.joints;

    float s   = std::sin(t);
    float s2  = std::sin(2*t);
    float c   = std::cos(t);

    // Hip sway
    j[(int)JointID::Hips].local_rot = euler_to_quat({0, 0, s * 4.0f});
    // Spine counter-rotation
    j[(int)JointID::Spine].local_rot = euler_to_quat({0, -s2 * 3.0f, -s * 2.0f});
    // Chest counter to spine
    j[(int)JointID::Chest].local_rot = euler_to_quat({0, s2 * 2.0f, 0});

    // Left leg: thigh swings forward/back, shin bends on backswing
    float thighL = s * 25.0f;
    float shinL  = std::max(0.0f, -std::sin(t + 0.4f) * 35.0f);
    j[(int)JointID::ThighL].local_rot = euler_to_quat({thighL, 0, 0});
    j[(int)JointID::ShinL].local_rot  = euler_to_quat({-shinL, 0, 0});

    // Right leg: opposite phase
    float thighR = -s * 25.0f;
    float shinR  = std::max(0.0f, std::sin(t + 0.4f) * 35.0f);
    j[(int)JointID::ThighR].local_rot = euler_to_quat({thighR, 0, 0});
    j[(int)JointID::ShinR].local_rot  = euler_to_quat({-shinR, 0, 0});

    // Arms swing opposite to legs
    j[(int)JointID::UpperArmL].local_rot = euler_to_quat({-thighL * 0.6f, 0, 15.0f});
    j[(int)JointID::UpperArmR].local_rot = euler_to_quat({-thighR * 0.6f, 0,-15.0f});
    j[(int)JointID::LowerArmL].local_rot = euler_to_quat({0,0,0});
    j[(int)JointID::LowerArmR].local_rot = euler_to_quat({0,0,0});

    // Foot angle to keep roughly flat
    j[(int)JointID::FootL].local_rot = euler_to_quat({-thighL * 0.3f + shinL * 0.5f, 0, 0});
    j[(int)JointID::FootR].local_rot = euler_to_quat({-thighR * 0.3f + shinR * 0.5f, 0, 0});
}

inline void apply_idle_pose(Skeleton& sk, float t) {
    auto& j = sk.joints;
    float breathe = std::sin(t * 0.8f) * 1.5f;
    j[(int)JointID::Chest].local_rot  = euler_to_quat({breathe, 0, 0});
    j[(int)JointID::Hips].local_rot   = euler_to_quat({0, std::sin(t*0.3f)*1.0f, 0});
    // Slight arm hang
    j[(int)JointID::UpperArmL].local_rot = euler_to_quat({0, 0,  20.0f});
    j[(int)JointID::UpperArmR].local_rot = euler_to_quat({0, 0, -20.0f});
    j[(int)JointID::LowerArmL].local_rot = euler_to_quat({10.0f, 0, 0});
    j[(int)JointID::LowerArmR].local_rot = euler_to_quat({10.0f, 0, 0});
}
```
</tool_response>

<tool_call>
{"name": "mcp", "arguments": {"tool": "code_find_definition", "args": "{\"symbol\": \"ActorRenderer\", \"file\": \"src/game/render/actor_renderer.h\"}"}}
</tool_call>
<tool_response>
File: src/game/render/actor_renderer.h
```cpp
#pragma once
#include "../actor.h"
#include "../../engine/gpu_context.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>

// One vertex in the stick-figure mesh
struct ActorVertex {
    glm::vec3 pos;
    glm::vec4 color;
};

struct BoneSegment {
    JointID start;
    JointID end;
    glm::vec4 color;
};

// Segments defining the stick figure skeleton
static const BoneSegment BONE_SEGMENTS[] = {