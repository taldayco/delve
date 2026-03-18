#ifndef LIGHTING_COMMON_GLSL
#define LIGHTING_COMMON_GLSL

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 2, binding = 0) readonly buffer LightBuffer {
    PointLight point_lights[];
};

layout(set = 2, binding = 1) readonly buffer LightGridBuffer {
    uvec2 light_grid[];  // (offset, count) per cluster
};

layout(set = 2, binding = 2) readonly buffer IndexBuffer {
    uint global_light_indices[];
};

float hex_dither(vec2 world_xy) {
    float sqrt3 = 1.7320508;
    float q = world_xy.x / sqrt3;
    float r = world_xy.y - q * 0.5;
    int iq = int(round(q));
    int ir = int(round(r));
    int is_val = int(round(-q - r));
    float dq = abs(float(iq) - q);
    float dr = abs(float(ir) - r);
    float ds = abs(float(is_val) - (-q - r));
    if (dq > dr && dq > ds) iq = -ir - is_val;
    else if (dr > ds)        ir = -iq - is_val;
    uint hash = uint(iq) * 374761393u ^ uint(ir) * 668265263u;
    return (float(hash & 0xFFu) / 255.0 - 0.5) * 0.25;
}

#ifndef PBR_LIGHTING_ONLY

vec3 apply_point_light(PointLight light, vec3 frag_pos, vec3 normal, vec3 base_color) {
    vec3  to_light = light.positionRadius.xyz - frag_pos;
    float dist     = length(to_light);
    float radius   = light.positionRadius.w;
    if (dist >= radius) return vec3(0.0);

    float atten = 1.0 - clamp(dist / radius, 0.0, 1.0);
    atten = atten * atten;

    vec3  L     = normalize(to_light);
    float NdotL = max(dot(normalize(normal), L), 0.0);

    return base_color * light.colorIntensity.rgb * light.colorIntensity.w * atten * NdotL;
}

vec3 apply_directional(vec3 color, vec3 normal) {
    float diffuse = max(dot(normalize(normal), light_dir.xyz), 0.0);
    return color * (light_dir.w + diffuse * light_col.rgb);
}

vec3 apply_star_ambient(vec3 color, vec3 normal, float sheen) {
    float NdotUp   = max(normal.z, 0.0);
    float star_int = star_light.w * mix(sheen * 0.2, 1.0, NdotUp);
    return color + star_light.rgb * star_int * sheen;
}

// Clustered point lighting with brute-force fallback for isometric projection.
// Requires COORD_FRAGMENT_STAGE (cluster_index() from coord.glsl).
vec3 clustered_point_lighting(vec3 frag_pos, vec3 normal, vec3 base_color) {
    vec3 result = vec3(0.0);

    uint cluster_idx = cluster_index();
    uvec2 grid_entry = light_grid[cluster_idx];
    uint offset = grid_entry.x;
    uint count  = grid_entry.y;

    // Cluster depth slicing is suboptimal for the isometric view matrix,
    // so some clusters miss lights. Fall back to brute-force when empty.
    if (count == 0u && LIGHT_COUNT > 0u) {
        uint total = min(LIGHT_COUNT, 128u);
        for (uint i = 0u; i < total; ++i) {
            result += apply_point_light(point_lights[i], frag_pos, normal, base_color);
        }
    } else {
        for (uint i = 0u; i < count; ++i) {
            uint light_idx = global_light_indices[offset + i];
            result += apply_point_light(point_lights[light_idx], frag_pos, normal, base_color);
        }
    }

    return result;
}

#endif // PBR_LIGHTING_ONLY

#endif // LIGHTING_COMMON_GLSL
