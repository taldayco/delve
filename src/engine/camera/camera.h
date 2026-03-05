#pragma once
#include "core/types.h"
#include <glm/glm.hpp>

struct CameraState {
  float world_x = 0.0f;
  float world_y = 0.0f;
  float zoom = 1.5f;
  float target_zoom = 1.5f;

  float follow_x = 0.0f;
  float follow_y = 0.0f;
  float follow_z = 0.0f;  // Z height for isometric projection centering
  bool following = false;

  float shake_intensity = 0.0f;
  float shake_duration = 0.0f;
  float shake_timer = 0.0f;

  float min_x = 0.0f, max_x = 128.0f;
  float min_y = 0.0f, max_y = 128.0f;
  float min_zoom = 0.25f, max_zoom = 8.0f;

  float follow_speed = 5.0f;
  float zoom_speed   = 5.0f;

  float base_frustum_half_w = 64.0f;
  float base_frustum_half_h = 64.0f;
  float near_plane = -500.0f;
  float far_plane  =  500.0f;
};

struct CameraMatrices {
  glm::mat4 view;
  glm::mat4 projection;
};

class CameraSystem {
public:
  void update(CameraState &cam, float dt);
  void follow(CameraState &cam, float target_x, float target_y);
  void stop_follow(CameraState &cam);
  void shake(CameraState &cam, float intensity, float duration);
  void set_zoom(CameraState &cam, float zoom);

  CameraMatrices build_matrices(const CameraState &cam, float aspect) const;
};
