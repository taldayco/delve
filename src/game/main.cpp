#include "topo_game.h"
#include "core/watchdog.h"
#include "agent_mode.h"
#include <csignal>
#include <cstdlib>
#include <thread>
#include <chrono>

static void emergency_signal_handler(int sig) {
  (void)sig;
  g_emergency_shutdown.store(true, std::memory_order_release);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::quick_exit(1);
}

int main(int argc, char *argv[]) {
  std::signal(SIGTERM, emergency_signal_handler);
  std::signal(SIGINT,  emergency_signal_handler);

  auto agent_config = parse_agent_args(argc, argv);

  TopoGame game;
  return game.run(agent_config);
}
