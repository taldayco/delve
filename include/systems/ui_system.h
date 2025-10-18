
#ifndef UI_SYSTEM_H
#define UI_SYSTEM_H

struct InputSystem;
struct MapGenerator;

enum UIButtonID { UI_BUTTON_NONE = -1, UI_BUTTON_NEW_MAP = 0, UI_BUTTON_COUNT };

struct UIButton {
  float x{}, y{}, w{}, h{};
  int id{UI_BUTTON_NONE};
};

struct UISystem {
  UIButton buttons[16]{};
  int button_count{};
  int hovered_button{UI_BUTTON_NONE};
  int clicked_button{UI_BUTTON_NONE};
};

void ui_init(UISystem *ui);
void ui_cleanup(UISystem *ui);
void ui_update(UISystem *ui, InputSystem *input, MapGenerator *map_gen);

bool ui_button_clicked(const UISystem *ui, int button_id);

#endif
