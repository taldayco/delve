#pragma once
#include <string>
#include <functional>
#include <unordered_map>

// JSON-line protocol over Unix domain socket.
// Each message: {"cmd":"...", "params":{...}}\n
// Each response: {"ok":true/false, "data":{...}}\n

class AgentServer {
public:
  using CommandHandler = std::function<std::string(const std::string &params_json)>;

  explicit AgentServer(const std::string &socket_path);
  ~AgentServer();

  // Register a command handler
  void register_command(const std::string &cmd, CommandHandler handler);

  // Start listening (non-blocking)
  bool start();

  // Poll for incoming connections/data (call each frame)
  void poll();

  // Stop and clean up
  void stop();

  bool is_running() const { return running; }

private:
  void accept_client();
  void process_client_data();
  void handle_message(const std::string &line);
  void send_response(const std::string &response);

  std::string socket_path;
  int listen_fd = -1;
  int client_fd = -1;
  bool running = false;
  std::string read_buffer;
  std::unordered_map<std::string, CommandHandler> handlers;
};
