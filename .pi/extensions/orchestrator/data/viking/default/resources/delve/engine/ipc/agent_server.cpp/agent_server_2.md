// Extract params (everything between "params": and the closing })
  std::string params_json = "{}";
  auto params_pos = line.find("\"params\"");
  if (params_pos != std::string::npos) {
    auto params_colon = line.find(':', params_pos + 8);
    if (params_colon != std::string::npos) {
      // Find the opening brace after the colon
      auto brace_start = line.find('{', params_colon + 1);
      if (brace_start != std::string::npos) {
        // Find matching closing brace
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

  // Dispatch to handler
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