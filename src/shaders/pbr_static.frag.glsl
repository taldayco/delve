#version 450

#define COORD_FRAGMENT_STAGE
#include "coord.glsl"
#include "pbr_common.glsl"
#define PBR_LIGHTING_ONLY
#include "lighting_common.glsl"

layout(location = 0) in vec3  frag_color;
layout(location = 1) in vec3  frag_world_pos;
layout(location = 2) in float frag_sheen;
layout(location = 3) in vec3  frag_normal;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 N = normalize(frag_normal);

    // Ortho projection: view direction is constant for all fragments.
    vec3 V = normalize(view_dir_ws.xyz);

    // Dither base color
    float dither = hex_dither(frag_world_pos.xy);
    vec3 albedo = clamp(frag_color * (1.0 + dither), 0.0, 1.0);

    // Material properties derived from vertex data
    float roughness = mix(0.9, 0.3, frag_sheen);
    float metallic  = frag_sheen * 0.1;

    // --- Directional light (Cook-Torrance) ---
    vec3 L_dir = normalize(light_dir.xyz);
    vec3 dir_radiance = light_col.rgb;
    vec3 color = cook_torrance_brdf(albedo, metallic, roughness, N, V, L_dir, dir_radiance);

    // Add ambient
    color += albedo * light_dir.w;

    // --- Point lights (clustered) ---
    uint cluster_idx = cluster_index();
    uvec2 grid_entry = light_grid[cluster_idx];
    uint offset = grid_entry.x;
    uint count  = grid_entry.y;

    // Cluster depth slicing is suboptimal for the isometric view matrix,
    // so some clusters miss lights. Fall back to brute-force when empty.
    if (count == 0u && LIGHT_COUNT > 0u) {
        count = min(LIGHT_COUNT, 128u);
        for (uint i = 0u; i < count; ++i) {
            PointLight pl = point_lights[i];
            vec3 to_light = pl.positionRadius.xyz - frag_world_pos;
            float dist = length(to_light);
            float radius = pl.positionRadius.w;
            if (dist >= radius) continue;

            float atten = 1.0 - clamp(dist / radius, 0.0, 1.0);
            atten = atten * atten;

            vec3 L = normalize(to_light);
            vec3 radiance = pl.colorIntensity.rgb * pl.colorIntensity.w * atten;
            color += cook_torrance_brdf(albedo, metallic, roughness, N, V, L, radiance);
        }
    } else {
        for (uint i = 0u; i < count; ++i) {
            uint light_idx = global_light_indices[offset + i];
            PointLight pl = point_lights[light_idx];
            vec3 to_light = pl.positionRadius.xyz - frag_world_pos;
            float dist = length(to_light);
            float radius = pl.positionRadius.w;
            if (dist >= radius) continue;

            float atten = 1.0 - clamp(dist / radius, 0.0, 1.0);
            atten = atten * atten;

            vec3 L = normalize(to_light);
            vec3 radiance = pl.colorIntensity.rgb * pl.colorIntensity.w * atten;
            color += cook_torrance_brdf(albedo, metallic, roughness, N, V, L, radiance);
        }
    }

    // Star ambient (artistic effect, preserved from original)
    float NdotUp   = max(N.z, 0.0);
    float star_int = star_light.w * mix(frag_sheen * 0.2, 1.0, NdotUp);
    color += star_light.rgb * star_int * frag_sheen;

    out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
