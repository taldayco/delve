# delve

A hex-grid topographic terrain game: procedurally generated basalt-column landscapes
with lava, contour lines, clustered lighting, and a skinned glTF character, rendered
with SDL3's GPU API (Vulkan).

## Build

Requirements: CMake ≥ 3.20, a C++20 compiler, SDL3, and `glslc` (from shaderc).
Dear ImGui is a git submodule; flecs, glm, and nlohmann-json are fetched by CMake.

```sh
git clone --recurse-submodules <repo-url>
cmake -B build
cmake --build build
./build/topogen
```

## Tests

```sh
cmake --build build --target delve_tests
./build/delve_tests
```
