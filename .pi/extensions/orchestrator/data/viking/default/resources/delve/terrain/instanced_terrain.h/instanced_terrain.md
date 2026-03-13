#pragma once
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

struct HexColumn;
struct TerrainState;

struct ColumnInstance {
    glm::mat4 model;
    float color_r, color_g, color_b, color_a;
};
static_assert(sizeof(ColumnInstance) == 80, "ColumnInstance must be 80 bytes");

class InstancedTerrain {
public:
    void build_instances(const std::vector<HexColumn> &columns,
                         const TerrainState &terrain);
    void upload(SDL_GPUDevice *device);
    void cleanup(SDL_GPUDevice *device);

    SDL_GPUBuffer *get_instance_ssbo() const { return instance_ssbo; }
    uint32_t       instance_count()    const { return num_instances; }
    bool           has_data()          const { return instance_ssbo && num_instances > 0; }

    std::vector<ColumnInstance> cpu_instances;
private:
    SDL_GPUBuffer *instance_ssbo = nullptr;
    uint32_t       num_instances = 0;
};
