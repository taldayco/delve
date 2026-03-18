#pragma once
#include <SDL3/SDL.h>
#include <string>

bool capture_frame_to_png(SDL_GPUDevice *device, SDL_Window *window, const std::string &output_path);
