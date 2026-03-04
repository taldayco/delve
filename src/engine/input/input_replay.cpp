#include "input_replay.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_map>

static const std::unordered_map<std::string, SDL_Scancode> KEY_NAME_MAP = {
  {"A", SDL_SCANCODE_A}, {"B", SDL_SCANCODE_B}, {"C", SDL_SCANCODE_C},
  {"D", SDL_SCANCODE_D}, {"E", SDL_SCANCODE_E}, {"F", SDL_SCANCODE_F},
  {"G", SDL_SCANCODE_G}, {"H", SDL_SCANCODE_H}, {"I", SDL_SCANCODE_I},
  {"J", SDL_SCANCODE_J}, {"K", SDL_SCANCODE_K}, {"L", SDL_SCANCODE_L},
  {"M", SDL_SCANCODE_M}, {"N", SDL_SCANCODE_N}, {"O", SDL_SCANCODE_O},
  {"P", SDL_SCANCODE_P}, {"Q", SDL_SCANCODE_Q}, {"R", SDL_SCANCODE_R},
  {"S", SDL_SCANCODE_S}, {"T", SDL_SCANCODE_T}, {"U", SDL_SCANCODE_U},
  {"V", SDL_SCANCODE_V}, {"W", SDL_SCANCODE_W}, {"X", SDL_SCANCODE_X},
  {"Y", SDL_SCANCODE_Y}, {"Z", SDL_SCANCODE_Z},
  {"Space", SDL_SCANCODE_SPACE}, {"Escape", SDL_SCANCODE_ESCAPE},
  {"Return", SDL_SCANCODE_RETURN}, {"Enter", SDL_SCANCODE_RETURN},
  {"Tab", SDL_SCANCODE_TAB},
  {"Up", SDL_SCANCODE_UP}, {"Down", SDL_SCANCODE_DOWN},
  {"Left", SDL_SCANCODE_LEFT}, {"Right", SDL_SCANCODE_RIGHT},
  {"1", SDL_SCANCODE_1}, {"2", SDL_SCANCODE_2}, {"3", SDL_SCANCODE_3},
  {"4", SDL_SCANCODE_4}, {"5", SDL_SCANCODE_5}, {"6", SDL_SCANCODE_6},
  {"7", SDL_SCANCODE_7}, {"8", SDL_SCANCODE_8}, {"9", SDL_SCANCODE_9},
  {"0", SDL_SCANCODE_0},
  {"F1", SDL_SCANCODE_F1}, {"F2", SDL_SCANCODE_F2}, {"F3", SDL_SCANCODE_F3},
  {"F4", SDL_SCANCODE_F4}, {"F5", SDL_SCANCODE_F5}, {"F6", SDL_SCANCODE_F6},
  {"Minus", SDL_SCANCODE_MINUS}, {"Equals", SDL_SCANCODE_EQUALS},
  {"LeftBracket", SDL_SCANCODE_LEFTBRACKET},
  {"RightBracket", SDL_SCANCODE_RIGHTBRACKET},
};

bool InputReplay::load(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open()) return false;

  nlohmann::json j;
  f >> j;

  if (!j.is_array()) return false;

  events.clear();
  for (auto &entry : j) {
    ReplayEvent ev;
    ev.frame = entry.value("frame", 0u);

    std::string key_name = entry.value("key", "");
    auto it = KEY_NAME_MAP.find(key_name);
    if (it == KEY_NAME_MAP.end()) continue;
    ev.scancode = it->second;

    std::string action = entry.value("action", "press");
    ev.key_down = (action == "press" || action == "down");

    events.push_back(ev);
  }

  // Sort by frame number
  std::sort(events.begin(), events.end(),
    [](const ReplayEvent &a, const ReplayEvent &b) { return a.frame < b.frame; });

  next_index = 0;
  loaded = true;
  return true;
}

void InputReplay::inject_events(uint32_t current_frame) {
  while (next_index < events.size() && events[next_index].frame <= current_frame) {
    const auto &ev = events[next_index];

    SDL_Event sdl_event = {};
    sdl_event.type = ev.key_down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    sdl_event.key.scancode = ev.scancode;
    sdl_event.key.down = ev.key_down;
    sdl_event.key.repeat = false;
    SDL_PushEvent(&sdl_event);

    ++next_index;
  }
}

bool InputReplay::is_done(uint32_t current_frame) const {
  return loaded && next_index >= events.size();
}
