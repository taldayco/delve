#include "agent_server.h"
#include <SDL3/SDL.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

AgentServer::AgentServer(const std::string &path) : socket_path(path) {}

AgentServer::~AgentServer() {
  stop();
}

void AgentServer::register_command(const std::string &cmd, CommandHandler handler) {
  handlers[cmd] = std::move(handler);
}

bool AgentServer::start() {
  // Remove stale socket
  unlink(socket_path.c_str());

  listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AgentServer: socket() failed: %s", strerror(errno));
    return false;
  }

  // Set non-blocking
  int flags = fcntl(listen_fd, F_GETFL, 0);
  fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AgentServer: bind() failed: %s", strerror(errno));
    close(listen_fd);
    listen_fd = -1;
    return false;
  }

  if (listen(listen_fd, 1) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AgentServer: listen() failed: %s", strerror(errno));
    close(listen_fd);
    listen_fd = -1;
    return false;
  }

  running = true;
  SDL_Log("AgentServer: listening on %s", socket_path.c_str());
  return true;
}

void AgentServer::poll() {
  if (!running) return;

  // Accept new client if none connected
  if (client_fd < 0) {
    accept_client();
  }

  // Read from connected client
  if (client_fd >= 0) {
    process_client_data();
  }
}

void AgentServer::accept_client() {
  int fd = accept(listen_fd, nullptr, nullptr);
  if (fd < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AgentServer: accept() failed: %s", strerror(errno));
    }
    return;
  }

  // Set non-blocking
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  // Only one client at a time
  if (client_fd >= 0) {
    close(client_fd);
  }
  client_fd = fd;
  read_buffer.clear();
  SDL_Log("AgentServer: client connected");
}

void AgentServer::process_client_data() {
  char buf[4096];
  while (true) {
    ssize_t n = read(client_fd, buf, sizeof(buf));
    if (n > 0) {
      read_buffer.append(buf, n);
    } else if (n == 0) {
      // Client disconnected
      SDL_Log("AgentServer: client disconnected");
      close(client_fd);
      client_fd = -1;
      read_buffer.clear();
      return;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AgentServer: read() failed: %s", strerror(errno));
        close(client_fd);
        client_fd = -1;
        read_buffer.clear();
        return;
      }
      break;
    }
  }

  // Process complete lines
  size_t pos;
  while ((pos = read_buffer.find('\n')) != std::string::npos) {
    std::string line = read_buffer.substr(0, pos);
    read_buffer.erase(0, pos + 1);
    if (!line.empty()) {
      handle_message(line);
    }
  }
}

void AgentServer::handle_message(const std::string &line) {
  // Minimal JSON parsing — extract "cmd" and "params" fields
  // Format: {"cmd":"name","params":{...}}

  // Find cmd value
  auto cmd_pos = line.find("\"cmd\"");
  if (cmd_pos == std::string::npos) {
    send_response("{\"ok\":false,\"error\":\"missing cmd field\"}\n");
    return;
  }

  // Extract cmd string value
  auto colon = line.find(':', cmd_pos + 5);
  auto quote1 = line.find('"', colon + 1);
  auto quote2 = line.find('"', quote1 + 1);
  if (quote1 == std::string::npos || quote2 == std::string::npos) {
    send_response("{\"ok\":false,\"error\":\"malformed cmd\"}\n");
    return;
  }
  std::string cmd = line.substr(quote1 + 1, quote2 - quote1 - 1);