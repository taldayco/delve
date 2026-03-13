#include "render/sprite.h"
#include <SDL3/SDL.h>

int SpriteManager::load_sheet(SDL_GPUDevice *device, const std::string &path,
                              int frame_width, int frame_height) {
  SDL_Surface *surface = SDL_LoadBMP(path.c_str());
  if (!surface) {
    SDL_Log("Failed to load sprite sheet: %s (%s)", path.c_str(), SDL_GetError());
    return -1;
  }

  SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA8888);
  SDL_DestroySurface(surface);
  if (!converted) {
    SDL_Log("Failed to convert sprite surface: %s", SDL_GetError());
    return -1;
  }

  int id = load_sheet_from_pixels(device,
      static_cast<const uint32_t *>(converted->pixels),
      converted->w, converted->h, frame_width, frame_height);

  SDL_DestroySurface(converted);
  return id;
}

int SpriteManager::load_sheet_from_pixels(SDL_GPUDevice *device,
                                          const uint32_t *pixels,
                                          int width, int height,
                                          int frame_width, int frame_height) {
  SpriteSheet sheet;
  sheet.texture = upload_pixels_to_texture(device, pixels, width, height);
  if (!sheet.texture.texture)
    return -1;

  sheet.frame_width = frame_width;
  sheet.frame_height = frame_height;

  int cols = width / frame_width;
  int rows = height / frame_height;

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      sheet.frames.push_back({
          c * frame_width, r * frame_height,
          frame_width, frame_height});
    }
  }

  int id = (int)sheets.size();
  sheets.push_back(std::move(sheet));
  return id;
}

const SpriteSheet *SpriteManager::get_sheet(int id) const {
  if (id < 0 || id >= (int)sheets.size())
    return nullptr;
  return &sheets[id];
}

void SpriteManager::cleanup(SDL_GPUDevice *device) {
  for (auto &sheet : sheets) {
    release_texture(device, sheet.texture);
  }
  sheets.clear();
}
