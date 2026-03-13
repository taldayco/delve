#pragma once
#include <SDL3/SDL.h>
#include <unordered_map>

enum class Action {
  MoveUp,
  MoveDown,
  MoveLeft,
  MoveRight,
  Interact,
  Cancel,
  Pause,
  ZoomIn,
  ZoomOut,
  CameraUp,
  CameraDown,
  CameraLeft,
  CameraRight,
  Count
};

struct InputState {
  bool held[(int)Action::Count] = {};
  bool pressed[(int)Action::Count] = {};
  bool released[(int)Action::Count] = {};

  float mouse_x = 0.0f;
  float mouse_y = 0.0f;
  float mouse_world_x = 0.0f;
  float mouse_world_y = 0.0f;
  bool mouse_left_pressed = false;
  bool mouse_left_held = false;
  bool mouse_right_pressed = false;
  bool mouse_right_held = false;
};

class InputSystem {
public:
  void init();
  void begin_frame();
  void handle_event(const SDL_Event &event);

  void bind(SDL_Scancode key, Action action);

  const InputState &state() const { return current; }
  InputState &state() { return current; }

private:
  InputState current;
  std::unordered_map<SDL_Scancode, Action> key_bindings;
};
