#include "map/map_generator.h"
#include "systems/input_system.h"
#include "systems/render_system.h"
#include "systems/ui_system.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vulkan/vulkan.h>

struct AppContext {
  GLFWwindow *window{};
  InputSystem input{};
  UISystem ui{};
  RenderSystem render{};
  MapGenerator map_gen{};
  bool running{true};
};

bool init_context(AppContext &ctx) {
  if (!glfwInit())
    return false;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  ctx.window = glfwCreateWindow(800, 600, "Map Generator", nullptr, nullptr);
  if (!ctx.window) {
    std::cout << "Failed to create window!\n"; // Add this debug
    glfwTerminate();
    return false;
  }

  std::cout << "Window created: " << ctx.window << "\n"; // Debug

  input_init(&ctx.input, ctx.window); // Must be AFTER window creation
  std::cout << "Input window stored: " << ctx.input.window << "\n"; // Debug

  ui_init(&ctx.ui);
  render_init(&ctx.render, ctx.window);
  ctx.map_gen.initialize();

  return true;
}

void shutdown_context(AppContext &ctx) {
  render_cleanup(&ctx.render);
  ui_cleanup(&ctx.ui);
  input_cleanup(&ctx.input);
  if (ctx.window)
    glfwDestroyWindow(ctx.window);
  glfwTerminate();
}
int main() {
  uint32_t vk_version = VK_API_VERSION_1_0;
  std::cout << "Vulkan API available: " << VK_VERSION_MAJOR(vk_version) << "."
            << VK_VERSION_MINOR(vk_version) << "\n";

  AppContext ctx{};
  if (!init_context(ctx)) {
    std::cout << "Failed to initialize context\n";
    return -1;
  }

  std::cout << "Entering main loop...\n";

  while (ctx.running) {
    glClear(GL_COLOR_BUFFER_BIT);
    input_update(&ctx.input);

    if (input_window_should_close(&ctx.input)) {
      std::cout << "Window close requested\n";
      ctx.running = false;
      break;
    }

    ui_update(&ctx.ui, &ctx.input, &ctx.map_gen);
    render_frame(&ctx.render, &ctx.ui, &ctx.map_gen);

    glfwSwapBuffers(ctx.window);
  }

  std::cout << "Exiting main loop...\n";
  shutdown_context(ctx);
  return 0;
}
