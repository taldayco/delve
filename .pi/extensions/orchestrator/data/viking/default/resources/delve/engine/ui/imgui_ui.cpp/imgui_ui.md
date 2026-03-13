#include "ui/imgui_ui.h"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

void ui_init(SDL_Window *window, SDL_GPUDevice *device) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();
  ImGui_ImplSDL3_InitForVulkan(window);

  ImGui_ImplSDLGPU3_InitInfo init_info = {};
  init_info.Device = device;
  init_info.ColorTargetFormat =
      SDL_GetGPUSwapchainTextureFormat(device, window);
  init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
  init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;

  ImGui_ImplSDLGPU3_Init(&init_info);
}

void ui_shutdown() {
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

void ui_process_event(SDL_Event &event) {
  ImGui_ImplSDL3_ProcessEvent(&event);
}

void ui_begin_frame() {
  ImGui_ImplSDLGPU3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void ui_end_frame() {
  ImGui::Render();
}

void ui_prepare_draw(SDL_GPUCommandBuffer *cmd) {
  ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
}

void ui_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *render_pass) {
  ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, render_pass);
}
