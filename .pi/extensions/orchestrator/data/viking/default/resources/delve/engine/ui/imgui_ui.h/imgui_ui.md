#pragma once
#include <SDL3/SDL.h>

struct SDL_GPUDevice;

void ui_init(SDL_Window *window, SDL_GPUDevice *device);
void ui_shutdown();
void ui_process_event(SDL_Event &event);
void ui_begin_frame();
void ui_end_frame();
void ui_prepare_draw(SDL_GPUCommandBuffer *cmd);
void ui_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *render_pass);
