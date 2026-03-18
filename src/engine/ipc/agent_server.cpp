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
  unlink(socket_path.c_str());

  listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AgentServer: socket() failed: %s", strerror(errno));
    return false;
  }

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

  if (client_fd < 0) {
    accept_client();
  }

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

  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

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
  auto cmd_pos = line.find("\"cmd\"");
  if (cmd_pos == std::string::npos) {
    send_response("{\"ok\":false,\"error\":\"missing cmd field\"}\n");
    return;
  }

  auto colon = line.find(':', cmd_pos + 5);
  auto quote1 = line.find('"', colon + 1);
  auto quote2 = line.find('"', quote1 + 1);
  if (quote1 == std::string::npos || quote2 == std::string::npos) {
    send_response("{\"ok\":false,\"error\":\"malformed cmd\"}\n");
    return;
  }
  std::string cmd = line.substr(quote1 + 1, quote2 - quote1 - 1);

  std::string params_json = "{}";
  auto params_pos = line.find("\"params\"");
  if (params_pos != std::string::npos) {
    auto params_colon = line.find(':', params_pos + 8);
    if (params_colon != std::string::npos) {
      auto brace_start = line.find('{', params_colon + 1);
      if (brace_start != std::string::npos) {
        int depth = 0;
        size_t end = brace_start;
        for (size_t i = brace_start; i < line.size(); ++i) {
          if (line[i] == '{') depth++;
          else if (line[i] == '}') {
            depth--;
            if (depth == 0) { end = i; break; }
          }
        }
        params_json = line.substr(brace_start, end - brace_start + 1);
      }
    }
  }

  auto it = handlers.find(cmd);
  if (it == handlers.end()) {
    send_response("{\"ok\":false,\"error\":\"unknown command: " + cmd + "\"}\n");
    return;
  }

  std::string result = it->second(params_json);
  send_response(result);
}

void AgentServer::send_response(const std::string &response) {
  if (client_fd < 0) return;

  std::string data = response;
  if (data.empty() || data.back() != '\n') data += '\n';

  size_t total = 0;
  while (total < data.size()) {
    ssize_t n = write(client_fd, data.data() + total, data.size() - total);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AgentServer: write() failed: %s", strerror(errno));
      break;
    }
    total += n;
  }
}

void AgentServer::stop() {
  running = false;
  if (client_fd >= 0) {
    close(client_fd);
    client_fd = -1;
  }
  if (listen_fd >= 0) {
    close(listen_fd);
    listen_fd = -1;
  }
  unlink(socket_path.c_str());
  SDL_Log("AgentServer: stopped");
}
