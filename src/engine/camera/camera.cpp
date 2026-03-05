#include "camera/camera.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>

static float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

void CameraSystem::update(CameraState &cam, float dt) {
  if (cam.following) {
    float t = 1.0f - std::exp(-cam.follow_speed * dt);
    cam.world_x = lerp(cam.world_x, cam.follow_x, t);
    cam.world_y = lerp(cam.world_y, cam.follow_y, t);
  }

  float zoom_t = 1.0f - std::exp(-cam.zoom_speed * dt);
  cam.zoom = lerp(cam.zoom, cam.target_zoom, zoom_t);

  cam.zoom = std::clamp(cam.zoom, cam.min_zoom, cam.max_zoom);
  cam.target_zoom = std::clamp(cam.target_zoom, cam.min_zoom, cam.max_zoom);

  cam.world_x = std::clamp(cam.world_x, cam.min_x, cam.max_x);
  cam.world_y = std::clamp(cam.world_y, cam.min_y, cam.max_y);

  if (cam.shake_timer > 0.0f) {
    cam.shake_timer -= dt;
    if (cam.shake_timer < 0.0f)
      cam.shake_timer = 0.0f;
  }
}

void CameraSystem::follow(CameraState &cam, float target_x, float target_y) {
  cam.follow_x = target_x;
  cam.follow_y = target_y;
  cam.following = true;
}

void CameraSystem::stop_follow(CameraState &cam) {
  cam.following = false;
}

void CameraSystem::shake(CameraState &cam, float intensity, float duration) {
  cam.shake_intensity = intensity;
  cam.shake_duration  = duration;
  cam.shake_timer     = duration;
}

void CameraSystem::set_zoom(CameraState &cam, float zoom) {
  cam.target_zoom = std::clamp(zoom, cam.min_zoom, cam.max_zoom);
}

CameraMatrices CameraSystem::build_matrices(const CameraState &cam, float aspect) const {
  CameraMatrices out;

  // Replicate the original CPU iso transform in tile-unit space:
  //   iso_x = (x - y) * TW       (TW=2, TH=1, HS=12.5 in tile units)
  //   iso_y = (x + y) * TH - z * HS
  //   iso_z = (x + y) * TH + z   (depth, increasing into screen)
  //
  // View matrix columns = where world X, Y, Z axes land in view space.
  // We keep iso_x as view-X, iso_y as view-Y, depth as view-Z.
  const float TW = 2.0f;
  const float TH = 1.0f;
  const float HS = 12.5f;  // ISO_HEIGHT_SCALE(100) / HEX_SIZE(8)

  // Column-major: col0=where world-X goes, col1=where world-Y goes, col2=where world-Z goes
  // world-X contributes: view_x += TW, view_y += TH, view_z += TH
  // world-Y contributes: view_x -= TW, view_y += TH, view_z += TH
  // world-Z contributes: view_x += 0,  view_y -= HS, view_z += 1
  out.view = glm::mat4(
    glm::vec4( TW,  TH,  TH, 0.0f),  // col 0: world X
    glm::vec4(-TW,  TH,  TH, 0.0f),  // col 1: world Y
    glm::vec4(0.0f,-HS, 1.0f, 0.0f), // col 2: world Z
    glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) // col 3: translation (none)
  );

  // Ortho frustum in iso/view space centred on camera.
  // Camera world pos maps to iso space via the same transform.
  float cam_iso_x = (cam.world_x - cam.world_y) * TW;
  float cam_iso_y = (cam.world_x + cam.world_y) * TH - cam.follow_z * HS;

  float shake_x = 0.0f, shake_y = 0.0f;
  if (cam.shake_timer > 0.0f) {
    float fade  = cam.shake_timer / cam.shake_duration;
    shake_x = ((float)(rand() % 1000) / 500.0f - 1.0f) * cam.shake_intensity * fade;
    shake_y = ((float)(rand() % 1000) / 500.0f - 1.0f) * cam.shake_intensity * fade;
  }
  cam_iso_x += shake_x;
  cam_iso_y += shake_y;

  float hw = cam.base_frustum_half_w / cam.zoom;
  float hh = cam.base_frustum_half_h / cam.zoom;
  if (aspect > 1.0f)
    hw = hh * aspect;
  else
    hh = hw / aspect;

  // Y axis: iso_y increases downward on screen, so bottom > top in glm::ortho
  out.projection = glm::ortho(
      cam_iso_x - hw, cam_iso_x + hw,
      cam_iso_y + hh, cam_iso_y - hh,
      cam.near_plane, cam.far_plane);

  return out;
}
