#include "systems/ui_system.h"
#include "map/map_generator.h"
#include "systems/input_system.h"
#include <GLFW/glfw3.h>

static bool button_test_click(const UIButton *btn, double mx, double my) {
  return mx >= btn->x && mx <= btn->x + btn->w && my >= btn->y &&
         my <= btn->y + btn->h;
}

void ui_init(UISystem *ui) {
  // Setup buttons
  ui->button_count = 0;

  // New Map button
  UIButton *btn = &ui->buttons[ui->button_count++];
  btn->id = UI_BUTTON_NEW_MAP;
  btn->x = 235.0f;
  btn->y = 101.0f;
  btn->w = 100.0f;
  btn->h = 40.0f;
}

void ui_cleanup(UISystem *ui) {
  // ZII: nothing to cleanup
  (void)ui;
}

void ui_update(UISystem *ui, InputSystem *input, MapGenerator *map_gen) {
  ui->clicked_button = UI_BUTTON_NONE;
  ui->hovered_button = UI_BUTTON_NONE;

  // Test all buttons
  for (int i = 0; i < ui->button_count; ++i) {
    UIButton *btn = &ui->buttons[i];

    if (button_test_click(btn, input->mouse_x, input->mouse_y)) {
      ui->hovered_button = btn->id;

      if (input_mouse_clicked(input)) {
        ui->clicked_button = btn->id;
      }
    }
  }

  // Handle button actions
  if (ui->clicked_button == UI_BUTTON_NEW_MAP) {
    map_gen->regenerate_map();
  }
  // Pan with middle mouse or right mouse
  static double last_mouse_x = 0.0;
  static double last_mouse_y = 0.0;
  static bool was_dragging = false;

  bool is_dragging =
      glfwGetMouseButton(input->window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

  if (is_dragging && was_dragging) {
    double dx = input->mouse_x - last_mouse_x;
    double dy = input->mouse_y - last_mouse_y;

    // Pan camera (inverted for natural feel)
    // Note: This needs render system access - we'll pass it through
    // For now, just store intent
  }

  last_mouse_x = input->mouse_x;
  last_mouse_y = input->mouse_y;
  was_dragging = is_dragging;

  // Keyboard shortcuts
  if (input_key_pressed(input, GLFW_KEY_N)) {
    map_gen->regenerate_map();
  }
  // Keyboard shortcuts
  if (input_key_pressed(input, GLFW_KEY_N)) {
    map_gen->regenerate_map();
  }
}

bool ui_button_clicked(const UISystem *ui, int button_id) {
  return ui->clicked_button == button_id;
}
