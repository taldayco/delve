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
  glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
  glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
  //
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

  ctx.map_gen.initialize();
  render_init(&ctx.render, ctx.window, &ctx.map_gen);

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
    // Test more keys
    if (glfwGetKey(ctx.window, GLFW_KEY_W) == GLFW_PRESS) {
      std::cout << "W key pressed!\n";
      ctx.render.camera_y += 2.0f;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_S) == GLFW_PRESS) {
      std::cout << "S key pressed!\n";
      ctx.render.camera_y -= 2.0f;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_A) == GLFW_PRESS) {
      std::cout << "A key pressed!\n";
      ctx.render.camera_x -= 2.0f;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_D) == GLFW_PRESS) {
      std::cout << "D key pressed!\n";
      ctx.render.camera_x += 2.0f;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_Q) == GLFW_PRESS) {
      std::cout << "Q key pressed - zoom in!\n";
      ctx.render.camera_zoom *= 1.02f;
    }
    if (glfwGetKey(ctx.window, GLFW_KEY_E) == GLFW_PRESS) {
      std::cout << "E key pressed - zoom out!\n";
      ctx.render.camera_zoom *= 0.98f;
    }

    if (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      ctx.running = false;
      break;
    }
    input_update(&ctx.input);

    if (input_window_should_close(&ctx.input)) {
      ctx.running = false;
      break;
    }
    // Throttle camera controls (only when keys pressed)
    static int frame_count = 0;
    if (frame_count % 2 == 0) { // Every other frame
      if (input_key_down(&ctx.input, GLFW_KEY_UP)) {
        ctx.render.camera_zoom *= 1.02f;
      }
      if (input_key_down(&ctx.input, GLFW_KEY_DOWN)) {
        ctx.render.camera_zoom *= 0.98f;
      }
      if (input_key_down(&ctx.input, GLFW_KEY_LEFT)) {
        ctx.render.camera_x -= 2.0f / ctx.render.camera_zoom;
      }
      if (input_key_down(&ctx.input, GLFW_KEY_RIGHT)) {
        ctx.render.camera_x += 2.0f / ctx.render.camera_zoom;
      }
    }
    frame_count++;

    ui_update(&ctx.ui, &ctx.input, &ctx.map_gen);
    render_frame(&ctx.render, &ctx.ui, &ctx.map_gen);

    glfwPollEvents();
  }

  std::cout << "Exiting main loop...\n";
  shutdown_context(ctx);
  return 0;
}
