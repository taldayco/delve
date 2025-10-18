
#ifndef INPUT_SYSTEM_H
#define INPUT_SYSTEM_H

struct GLFWwindow;

struct InputSystem {
  GLFWwindow *window{};
  double mouse_x{};
  double mouse_y{};
  bool mouse_down{};
  bool mouse_was_down{};
  bool keys[512]{};
  bool keys_last[512]{};
};

void input_init(InputSystem *input, GLFWwindow *window);
void input_cleanup(InputSystem *input);
void input_update(InputSystem *input);

bool input_window_should_close(const InputSystem *input);
bool input_key_pressed(const InputSystem *input, int key);
bool input_key_down(const InputSystem *input, int key);
bool input_mouse_clicked(const InputSystem *input);

#endif
