// test_actor_scale.cpp
// Tests for actor rendering scale correctness and camera tracking behavior.
//
//  actor_scale_sane       — the root transform scale applied to the skinned
//                           character (Config::HEX_SIZE * 0.2f) produces a
//                           world-unit scale within a sane rendering range.
//  camera_tracks_actor    — CameraSystem::follow() + update() converges the
//                           camera position toward the actor target within a
//                           few frames.

#include "test_harness.h"
#include "config.h"
#include "camera/camera.h"
#include <cmath>

// ── actor_scale_sane ──────────────────────────────────────────────────────────
// The skinned character root transform applies:
//   scale = Config::HEX_SIZE * 0.2f
// A glTF character in Blender meters (~1.8 m tall) rendered at this scale
// should occupy a meaningful fraction of a hex tile (HEX_SIZE world units).
// Acceptable range: [0.5, 20.0] world-unit scale factor.

DELVE_TEST(actor_scale_sane) {
    float scale = Config::HEX_SIZE * 0.2f;
    EXPECT_GE(scale, 0.5f);
    EXPECT_GT(20.1f, scale);
    // Must be strictly positive
    EXPECT_GT(scale, 0.0f);
    return true;
}

// ── camera_tracks_actor ───────────────────────────────────────────────────────
// After calling CameraSystem::follow() with a target position and advancing
// several frames at dt=0.016s, the camera world position should converge
// toward the target.  After 60 frames (~1 s) the distance must be < 1.0f.

DELVE_TEST(camera_tracks_actor) {
    CameraState cam;
    cam.world_x      = 0.0f;
    cam.world_y      = 0.0f;
    cam.follow_speed = 5.0f;
    cam.min_x = -1000.0f; cam.max_x = 1000.0f;
    cam.min_y = -1000.0f; cam.max_y = 1000.0f;

    const float target_x = 32.0f;
    const float target_y = 16.0f;

    CameraSystem cam_sys;
    cam_sys.follow(cam, target_x, target_y);

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) {
        cam_sys.update(cam, dt);
    }

    float dx = cam.world_x - target_x;
    float dy = cam.world_y - target_y;
    float dist = std::sqrt(dx * dx + dy * dy);
    EXPECT_LT(dist, 1.0f);
    return true;
}
