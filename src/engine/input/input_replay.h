#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>

struct ReplayEvent {
  uint32_t frame;       // Frame number to inject this event
  SDL_Scancode scancode;
  bool key_down;        // true = press, false = release
};

class InputReplay {
public:
  bool load(const std::string &path);

  // Call each frame. Pushes SDL events for the current frame.
  void inject_events(uint32_t current_frame);

  bool is_done(uint32_t current_frame) const;
  bool is_loaded() const { return loaded; }
  size_t event_count() const { return events.size(); }

private:
  std::vector<ReplayEvent> events;
  size_t next_index = 0;
  bool loaded = false;
};
