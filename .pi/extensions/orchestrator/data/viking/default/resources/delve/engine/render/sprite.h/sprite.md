#pragma once
#include "gpu/gpu.h"
#include <cstdint>
#include <string>
#include <vector>

struct SpriteFrame {
  int x, y, w, h;
};

struct SpriteSheet {
  TextureHandle texture;
  std::vector<SpriteFrame> frames;
  int frame_width = 0;
  int frame_height = 0;
};

struct Animation {
  int start_frame = 0;
  int frame_count = 1;
  float frame_duration = 0.1f;
  bool looping = true;
};

struct SpriteComponent {
  int sheet_id = -1;
  int current_frame = 0;
  float anim_timer = 0.0f;
  Animation current_anim;
  bool flip_x = false;
};

struct PositionComponent {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

class SpriteManager {
public:
  int load_sheet(SDL_GPUDevice *device, const std::string &path,
                 int frame_width, int frame_height);

  int load_sheet_from_pixels(SDL_GPUDevice *device, const uint32_t *pixels,
                             int width, int height,
                             int frame_width, int frame_height);

  const SpriteSheet *get_sheet(int id) const;

  void cleanup(SDL_GPUDevice *device);

private:
  std::vector<SpriteSheet> sheets;
};
