#include "terrain/instanced_terrain.h"
#include "terrain/map_data.h"
#include "terrain/hex.h"
#include "terrain/palettes.h"
#include "game_state.h"
#include "config.h"
#include "gpu/gpu.h"
#include <glm/gtc/matrix_transform.hpp>

static void color_to_float(uint32_t c, float &r, float &g, float &b) {
    r = ((c >> 16) & 0xFF) / 255.0f;
    g = ((c >>  8) & 0xFF) / 255.0f;
    b = ((c      ) & 0xFF) / 255.0f;
}

void InstancedTerrain::build_instances(const std::vector<HexColumn> &columns,
                                        const TerrainState &terrain) {
    cpu_instances.clear();
    cpu_instances.reserve(columns.size());

    int pal_idx = std::clamp(terrain.current_palette, 0, PALETTE_COUNT - 1);
    const Palette &palette = PALETTES[pal_idx];

    for (const auto &col : columns) {
        float wx, wy;
        hex_to_pixel(col.q, col.r, Config::HEX_SIZE, wx, wy);
        wx /= Config::HEX_SIZE;
        wy /= Config::HEX_SIZE;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(wx, wy, 0.0f));
        model = glm::scale(model, glm::vec3(1.0f, 1.0f, col.height));

        uint32_t color = organic_color(col.base_height, col.q, col.r, palette);
        float cr, cg, cb;
        color_to_float(color, cr, cg, cb);

        cpu_instances.push_back({model, cr, cg, cb, 1.0f});
    }

}

void InstancedTerrain::upload(SDL_GPUDevice *device) {
    if (cpu_instances.empty()) {
        num_instances = 0;
        return;
    }

    if (instance_ssbo) {
        SDL_ReleaseGPUBuffer(device, instance_ssbo);
        instance_ssbo = nullptr;
    }

    uint32_t byte_size = (uint32_t)(cpu_instances.size() * sizeof(ColumnInstance));
    instance_ssbo = gpu_upload_buffer(device, cpu_instances.data(), byte_size,
                                       SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    num_instances = (uint32_t)cpu_instances.size();
    cpu_instances.clear();

    SDL_Log("InstancedTerrain: Uploaded %u instances (%.1f KB)",
            num_instances, byte_size / 1024.0f);
}

void InstancedTerrain::cleanup(SDL_GPUDevice *device) {
    if (instance_ssbo) {
        SDL_ReleaseGPUBuffer(device, instance_ssbo);
        instance_ssbo = nullptr;
    }
    num_instances = 0;
}
