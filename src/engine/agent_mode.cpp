#include "agent_mode.h"
#include <cstring>

AgentModeConfig parse_agent_args(int argc, char *argv[]) {
  AgentModeConfig config;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--agent-mode") == 0) {
      config.enabled = true;
    } else if (std::strcmp(argv[i], "--agent-socket") == 0 && i + 1 < argc) {
      config.socket_path = argv[++i];
    } else if (std::strcmp(argv[i], "--replay-input") == 0 && i + 1 < argc) {
      config.replay_input_path = argv[++i];
    }
  }

  return config;
}
