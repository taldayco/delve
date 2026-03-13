SDL_Log("Entering main loop");
  uint64_t freq = SDL_GetPerformanceFrequency();
  uint64_t prev_time = SDL_GetPerformanceCounter();
  float accumulator = 0.0f;

  while (running) {
    if (g_emergency_shutdown.load(std::memory_order_relaxed)) {
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                      "Emergency shutdown flag detected — exiting main loop");
      break;
    }

    // Poll agent server for IPC commands
    if (agent_server) {
      agent_server->poll();
    }

    uint64_t current_time = SDL_GetPerformanceCounter();
    float frame_time = (float)(current_time - prev_time) / (float)freq;
    prev_time = current_time;

    if (frame_time > MAX_FRAME_TIME)
      frame_time = MAX_FRAME_TIME;

    accumulator += frame_time;


    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ui_process_event(event);

      if (event.type == SDL_EVENT_QUIT)
        running = false;

      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        SDL_WindowID tool_id = SDL_GetWindowID(gpu_ctx.window);
        if (event.window.windowID == tool_id) {
          running = false;
        } else {
          gpu_destroy_game_window(gpu_ctx);
        }
      }

      on_event(event, ecs_world);
    }


    if (wants_game_window_open(ecs_world)) {
      gpu_create_game_window(gpu_ctx);
    }
    if (wants_game_window_close(ecs_world)) {
      gpu_destroy_game_window(gpu_ctx);
    }


    while (accumulator >= FIXED_DT) {
      ecs_world.progress(FIXED_DT);
      accumulator -= FIXED_DT;
    }


    asset_manager.check_for_updates();

    FrameContext tool_frame;
    if (gpu_acquire_frame(gpu_ctx, tool_frame)) {
      on_render_tool(gpu_ctx, tool_frame, ecs_world);
      gpu_end_frame(tool_frame);
    }


    if (gpu_ctx.game_window) {
      on_pre_frame_game(gpu_ctx, ecs_world);
      FrameContext game_frame;
      if (gpu_acquire_game_frame(gpu_ctx, game_frame)) {
        on_render_game(gpu_ctx, game_frame, ecs_world);
        gpu_end_frame(game_frame);
      }
    }
  }

  stop_watchdog();
  on_cleanup(ecs_world);
  asset_manager.clear();
  ui_shutdown();
  gpu_cleanup(gpu_ctx);
  return 0;
}