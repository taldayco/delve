#version 450

layout(local_size_x = 16, local_size_y = 9, local_size_z = 1) in;

layout(set = 1, binding = 0) uniform CullUniforms {
    float tile_px;
    float grid_size_x;
    float grid_size_y;
    float num_slices;

    float near_plane;
    float far_plane;
    float screen_w;
    float screen_h;

    float light_count_f;
    float _pad0;
    float _pad1;
    float _pad2;
};

layout(set = 1, binding = 1) uniform CullMatrices {
    mat4 view_proj;
};

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

struct ClusterAABB {
    vec4 minPoint;
    vec4 maxPoint;
};

struct LightGridEntry {
    uint offset;
    uint count;
};

layout(set = 0, binding = 0) buffer LightBuffer     { PointLight    lights[];           };
layout(set = 0, binding = 1) buffer ClusterBuffer   { ClusterAABB  clusters[];          };
layout(set = 0, binding = 2) buffer LightGridBuffer { LightGridEntry lightGrid[];       };
layout(set = 0, binding = 3) buffer IndexBuffer     { uint globalLightIndexList[];      };
layout(set = 0, binding = 4) buffer CounterBuffer   { uint globalIndexCount;            };

float sqDistPointAABB(vec3 p, vec3 aabbMin, vec3 aabbMax) {
    float sqDist = 0.0;
    for (int i = 0; i < 3; ++i) {
        float v = p[i];
        if (v < aabbMin[i]) sqDist += (aabbMin[i] - v) * (aabbMin[i] - v);
        if (v > aabbMax[i]) sqDist += (v - aabbMax[i]) * (v - aabbMax[i]);
    }
    return sqDist;
}

bool sphereAABB(vec3 center, float radius, vec3 aabbMin, vec3 aabbMax) {
    return sqDistPointAABB(center, aabbMin, aabbMax) <= radius * radius;
}

vec3 world_to_cluster_space(vec3 world_pos) {
    vec4 clip = view_proj * vec4(world_pos, 1.0);
    vec3 ndc  = clip.xyz / clip.w;
    return ndc;
}

void main() {
    uvec3 id    = gl_GlobalInvocationID;
    uint  gridX = uint(grid_size_x);
    uint  gridY = uint(grid_size_y);

    if (id.x >= gridX || id.y >= gridY || id.z >= uint(num_slices))
        return;

    uint clusterIdx = id.x + id.y * gridX + id.z * gridX * gridY;

    vec3 aabbMin = clusters[clusterIdx].minPoint.xyz;
    vec3 aabbMax = clusters[clusterIdx].maxPoint.xyz;

    uint visibleIndices[128];
    uint visibleCount = 0;

    uint light_count = uint(light_count_f);
    for (uint i = 0; i < light_count && i < 1024u; ++i) {
        vec3  lpos    = world_to_cluster_space(lights[i].positionRadius.xyz);
        float lradius = lights[i].positionRadius.w * 0.01;

        vec3 aabbMinZ = vec3(aabbMin.xy, aabbMin.z - 0.5);
        vec3 aabbMaxZ = vec3(aabbMax.xy, aabbMax.z + 0.5);

        float intensity = lights[i].colorIntensity.w;
        if (intensity > 0.0 && sphereAABB(lpos, lradius, aabbMinZ, aabbMaxZ)) {
            if (visibleCount < 128u) {
                visibleIndices[visibleCount] = i;
                visibleCount++;
            }
        }
    }

    const uint MAX_INDICES = 65536u;
    uint offset = atomicAdd(globalIndexCount, visibleCount);

    if (offset + visibleCount > MAX_INDICES) {
        if (offset < MAX_INDICES) {
            uint canFit = MAX_INDICES - offset;
            for (uint i = 0; i < canFit; ++i)
                globalLightIndexList[offset + i] = visibleIndices[i];
            lightGrid[clusterIdx].offset = offset;
            lightGrid[clusterIdx].count  = canFit;
        } else {
            lightGrid[clusterIdx].offset = 0;
            lightGrid[clusterIdx].count  = 0;
        }
        return;
    }

    for (uint i = 0; i < visibleCount; ++i)
        globalLightIndexList[offset + i] = visibleIndices[i];

    lightGrid[clusterIdx].offset = offset;
    lightGrid[clusterIdx].count  = visibleCount;
}
