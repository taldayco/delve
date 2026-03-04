cmake_minimum_required(VERSION 3.20)
project(TopographyGen)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Dependencies
option(DELVE_HEADLESS_ONLY "Build only headless targets (no SDL/GPU)" OFF)

if(NOT DELVE_HEADLESS_ONLY)
  find_package(SDL3 REQUIRED)
endif()

include(FetchContent)

FetchContent_Declare(flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs
    GIT_TAG v4.0.5
)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG v3.11.3
)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG 1.0.1
)

FetchContent_MakeAvailable(flecs json glm)

if(NOT DELVE_HEADLESS_ONLY)

# ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# ---- Shader compilation ----
find_program(GLSLC glslc REQUIRED)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

set(GRAPHICS_SHADER_SOURCES
    ${SHADER_DIR}/terrain.vert.glsl
    ${SHADER_DIR}/terrain.frag.glsl
    ${SHADER_DIR}/lava.vert.glsl
    ${SHADER_DIR}/lava.frag.glsl
    ${SHADER_DIR}/contour.vert.glsl
    ${SHADER_DIR}/contour.frag.glsl
    ${SHADER_DIR}/background.vert.glsl
    ${SHADER_DIR}/background.frag.glsl
    ${SHADER_DIR}/actor_skel.vert.glsl
    ${SHADER_DIR}/actor_skel.frag.glsl
    ${SHADER_DIR}/actor_mesh.vert.glsl
    ${SHADER_DIR}/actor_mesh.frag.glsl
)

set(COMPUTE_SHADER_SOURCES
    ${SHADER_DIR}/generate_clusters.comp.glsl
    ${SHADER_DIR}/light_culling.comp.glsl
)

# Shared include files — any change here forces recompilation of all shaders
set(SHADER_INCLUDES
    ${SHADER_DIR}/coord.glsl
    ${SHADER_DIR}/lighting_common.glsl
)

set(SHADER_OUTPUTS)

# Graphics shaders (vert/frag)
foreach(SHADER ${GRAPHICS_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE ${SHADER_OUT_DIR}/${SHADER_NAME}.spv)
    string(REGEX MATCH "\\.(vert|frag)\\." STAGE_MATCH ${SHADER_NAME})
    string(REGEX REPLACE "\\." "" SHADER_STAGE ${STAGE_MATCH})
    if(SHADER_STAGE STREQUAL "vert")
        set(STAGE_FLAG "-fshader-stage=vertex")
    else()
        set(STAGE_FLAG "-fshader-stage=fragment")
    endif()
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC} ${STAGE_FLAG} -I ${SHADER_DIR} -o ${SPV_FILE} ${SHADER}
        DEPENDS ${SHADER} ${SHADER_INCLUDES}
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_OUTPUTS ${SPV_FILE})
endforeach()

# Compute shaders
foreach(SHADER ${COMPUTE_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE ${SHADER_OUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC} -fshader-stage=compute -I ${SHADER_DIR} -o ${SPV_FILE} ${SHADER}
        DEPENDS ${SHADER} ${SHADER_INCLUDES}
        COMMENT "Compiling compute shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_OUTPUTS ${SPV_FILE})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})

# ---- topo_engine (static library) ----
add_library(topo_engine STATIC
    src/engine/app.cpp
    src/engine/agent_mode.cpp
    src/engine/core/asset_manager.cpp
    src/engine/core/task_system.cpp
    src/engine/core/watchdog.cpp
    src/engine/gpu/gpu.cpp
    src/engine/gpu/frame_capture.cpp
    src/engine/ipc/agent_server.cpp
    src/engine/input/input.cpp
    src/engine/input/input_replay.cpp
    src/engine/camera/camera.cpp
    src/engine/ui/imgui_ui.cpp
    src/engine/render/sprite.cpp
    src/engine/render/render_system.cpp
    src/engine/render/background.cpp
)

target_include_directories(topo_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
)

target_link_libraries(topo_engine PUBLIC
    SDL3::SDL3
    imgui
    flecs::flecs_static
    glm::glm
    nlohmann_json::nlohmann_json
)

# ---- topogen (executable) ----
add_executable(topogen
    src/game/main.cpp
    src/game/topo_game.cpp
    src/game/terrain/noise.cpp
    src/game/terrain/noise_layers.cpp
    src/game/terrain/noise_composer.cpp
    src/game/terrain/contour.cpp
    src/game/terrain/hex.cpp
    src/game/terrain/isometric.cpp
    src/game/terrain/basalt.cpp
    src/game/terrain/lava.cpp
    src/game/terrain/detail.cpp
    src/game/terrain/delve_render.cpp
    src/game/terrain/map_gen.cpp
    src/game/terrain/terrain_mesh.cpp
    src/game/terrain/terrain_renderer.cpp
    src/game/terrain/map_util.cpp
    src/game/render/actor_renderer.cpp
    src/game/render/actor_animation.cpp
)

target_include_directories(topogen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
)

target_compile_definitions(topogen PRIVATE
    SHADER_DIR="${SHADER_OUT_DIR}"
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_compile_definitions(topo_engine PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_link_libraries(topogen PRIVATE
    topo_engine
    nlohmann_json::nlohmann_json
    glm::glm
)

add_dependencies(topogen shaders)

endif() # NOT DELVE_HEADLESS_ONLY

# ---- Headless terrain pipeline sources (shared by test + metrics targets) ----
set(TERRAIN_PIPELINE_SOURCES
    src/game/terrain/noise.cpp
    src/game/terrain/noise_layers.cpp
    src/game/terrain/noise_composer.cpp
    src/game/terrain/contour.cpp
    src/game/terrain/hex.cpp
    src/game/terrain/basalt.cpp
    src/game/terrain/lava.cpp
    src/game/terrain/terrain_mesh.cpp
)

# ---- delve_tests (headless test runner) ----
add_executable(delve_tests EXCLUDE_FROM_ALL
    src/test/test_main.cpp
    src/test/tests/test_noise.cpp
    src/test/tests/test_hex.cpp
    src/test/tests/test_animation.cpp
    src/test/tests/test_palettes.cpp
    src/test/tests/test_terrain_pipeline.cpp
    src/test/tests/test_color.cpp
    src/test/tests/test_map_util.cpp
    src/test/tests/test_skeleton_mesh.cpp
    src/game/terrain/map_util.cpp
    src/game/render/skeleton_mesh.cpp
    ${TERRAIN_PIPELINE_SOURCES}
)

target_include_directories(delve_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/test/sdl_override  # Must be FIRST to intercept <SDL3/SDL.h>
    ${CMAKE_CURRENT_SOURCE_DIR}/src/test
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
)

target_compile_definitions(delve_tests PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    DELVE_HEADLESS=1
)

target_link_libraries(delve_tests PRIVATE
    glm::glm
)

# ---- delve_metrics (headless metrics binary) ----
add_executable(delve_metrics EXCLUDE_FROM_ALL
    src/test/metrics_main.cpp
    ${TERRAIN_PIPELINE_SOURCES}
)

target_include_directories(delve_metrics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/test/sdl_override
    ${CMAKE_CURRENT_SOURCE_DIR}/src/test
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
)

target_compile_definitions(delve_metrics PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    DELVE_HEADLESS=1
)

target_link_libraries(delve_metrics PRIVATE
    glm::glm
    nlohmann_json::nlohmann_json
)
