#include "systems/input_system.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <iostream>

void input_init(InputSystem *input, GLFWwindow *window) {
  input->window = window;
  std::cout << "input_init: storing window " << window << "\n"; // Debug
}

bool input_window_should_close(const InputSystem *input) {
  return glfwWindowShouldClose(input->window);
}

void input_cleanup(InputSystem *input) {
  // Nothing to cleanup - ZII
  (void)input;
}

void input_update(InputSystem *input) { // Remove window parameter
  // Store previous frame
  std::memcpy(input->keys_last, input->keys, sizeof(input->keys));
  input->mouse_was_down = input->mouse_down;

  // Update current frame using stored window
  glfwGetCursorPos(input->window, &input->mouse_x, &input->mouse_y);
  input->mouse_down =
      glfwGetMouseButton(input->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

  // Update key states
  for (int i = 0; i < 512; ++i) {
    input->keys[i] = glfwGetKey(input->window, i) == GLFW_PRESS;
  }
}

bool input_window_should_close(const InputSystem *input, GLFWwindow *window) {
  (void)input;
  return glfwWindowShouldClose(window);
}

bool input_key_pressed(const InputSystem *input, int key) {
  if (key < 0 || key >= 512)
    return false;
  return input->keys[key] && !input->keys_last[key];
}

bool input_key_down(const InputSystem *input, int key) {
  if (key < 0 || key >= 512)
    return false;
  return input->keys[key];
}

bool input_mouse_clicked(const InputSystem *input) {
  return input->mouse_down && !input->mouse_was_down;
}
