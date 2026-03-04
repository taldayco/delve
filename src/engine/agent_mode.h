#pragma once
#include <string>

struct AgentModeConfig {
  bool enabled = false;
  std::string socket_path = "/tmp/delve-agent.sock";
};

AgentModeConfig parse_agent_args(int argc, char *argv[]);
