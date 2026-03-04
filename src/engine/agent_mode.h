#pragma once
#include <string>

struct AgentModeConfig {
  bool enabled = false;
  std::string socket_path = "/tmp/delve-agent.sock";
  std::string replay_input_path; // --replay-input <path>: replay input events from JSON file
};

AgentModeConfig parse_agent_args(int argc, char *argv[]);
