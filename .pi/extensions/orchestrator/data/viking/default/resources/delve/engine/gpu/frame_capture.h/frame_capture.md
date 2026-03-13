#pragma once
#include <SDL3/SDL.h>
#include <string>

// Capture the current frame from a window's swapchain to a PNG file.
// Uses SDL3-GPU texture readback via transfer buffer + stb_image_write.
bool capture_frame_to_png(SDL_GPUDevice *device, SDL_Window *window, const std::string &output_path);
