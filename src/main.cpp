#include "include/map/map_generator.h"
#include <GLFW/glfw3.h>
#include <iostream>

// Simple button state
struct Button {
  float x, y, w, h;
  bool was_pressed{};

  bool check_click(double mouse_x, double mouse_y, bool mouse_down) {
    bool inside =
        mouse_x >= x && mouse_x <= x + w && mouse_y >= y && mouse_y <= y + h;
    bool clicked = inside && mouse_down && !was_pressed;
    was_pressed = mouse_down;
    return clicked;
  }
};

void print_map_stats(const MapGenerator::MapData &data) {
  std::cout << "\n=== Map Stats ===\n";
  std::cout << "Dimensions: " << data.width << "x" << data.height << "\n";
  std::cout << "Total nodes: " << data.nodes->size() << "\n";

  // Count types
  int type_counts[6] = {};
  for (const auto &node : *data.nodes) {
    type_counts[static_cast<int>(node.type)]++;
  }

  std::cout << "Enemy: " << type_counts[1] << "\n";
  std::cout << "Shelter: " << type_counts[3] << "\n";
  std::cout << "Wenny: " << type_counts[4] << "\n";
  std::cout << "Boss: " << type_counts[5] << "\n";
  std::cout << "=================\n\n";
}

int main() {
  // Initialize GLFW
  if (!glfwInit())
    return -1;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // For future Vulkan
  GLFWwindow *window =
      glfwCreateWindow(800, 600, "Map Generator", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return -1;
  }

  // Initialize map
  MapGenerator map_gen;
  map_gen.initialize();
  print_map_stats(map_gen.get_map_data());

  Button new_game_btn{235, 101, 100, 40};

  // Main loop
  while (!glfwWindowShouldClose(window)) {
    // Input
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    bool mouse_down =
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    if (new_game_btn.check_click(mx, my, mouse_down)) {
      std::cout << "Regenerating map...\n";
      map_gen.regenerate_map();
      print_map_stats(map_gen.get_map_data());
    }

    // Or use keyboard
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS) {
      map_gen.regenerate_map();
      print_map_stats(map_gen.get_map_data());
    }

    glfwPollEvents();
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
