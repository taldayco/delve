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
    float ndc_radius_scale;
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

bool aabbOverlap(vec3 aMin, vec3 aMax, vec3 bMin, vec3 bMax) {
    return aMin.x <= bMax.x && aMax.x >= bMin.x &&
           aMin.y <= bMax.y && aMax.y >= bMin.y &&
           aMin.z <= bMax.z && aMax.z >= bMin.z;
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
        vec3 center = lights[i].positionRadius.xyz;
        float r = lights[i].positionRadius.w;
        float r_aabb = r * 1.7321;
        vec3 ndc_px = world_to_cluster_space(center + vec3(r_aabb, 0, 0));
        vec3 ndc_nx = world_to_cluster_space(center - vec3(r_aabb, 0, 0));
        vec3 ndc_py = world_to_cluster_space(center + vec3(0, r_aabb, 0));
        vec3 ndc_ny = world_to_cluster_space(center - vec3(0, r_aabb, 0));
        vec3 ndc_pz = world_to_cluster_space(center + vec3(0, 0, r_aabb));
        vec3 ndc_nz = world_to_cluster_space(center - vec3(0, 0, r_aabb));
        vec3 light_min = min(min(min(ndc_px, ndc_nx), min(ndc_py, ndc_ny)), min(ndc_pz, ndc_nz));
        vec3 light_max = max(max(max(ndc_px, ndc_nx), max(ndc_py, ndc_ny)), max(ndc_pz, ndc_nz));
        float intensity = lights[i].colorIntensity.w;
        if (intensity > 0.0 && aabbOverlap(light_min, light_max, aabbMin, aabbMax)) {
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
