#ifndef LIGHTING_COMMON_GLSL
#define LIGHTING_COMMON_GLSL

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 2, binding = 0) uniform sampler2D terrain_light_tex;

layout(set = 2, binding = 1) uniform sampler2D rc_fluence;

layout(set = 2, binding = 2) readonly buffer LightBuffer {
    PointLight point_lights[];
};

layout(set = 2, binding = 3) readonly buffer LightGridBuffer {
    uvec2 light_grid[];
};

layout(set = 2, binding = 4) readonly buffer IndexBuffer {
    uint global_light_indices[];
};

const float TL_HEIGHT_RANGE = 1.25;
const float TL_HEIGHT_TOL   = 0.02;

vec2 sample_terrain_light(vec2 world_xy, float world_z) {
    vec2 ts = vec2(textureSize(terrain_light_tex, 0));
    vec2 p  = world_xy * INV_MAP_UNITS * ts;
    vec2 b  = floor(p);
    vec2 f  = p - b;

    vec2  acc   = vec2(0.0);
    vec2  bilin = vec2(0.0);
    float wsum  = 0.0;
    for (int i = 0; i < 4; ++i) {
        ivec2 o = ivec2(i & 1, i >> 1);
        vec4  t = texelFetch(terrain_light_tex,
                             clamp(ivec2(b) + o, ivec2(0), ivec2(ts) - 1), 0);
        float bw = (o.x == 0 ? 1.0 - f.x : f.x) * (o.y == 0 ? 1.0 - f.y : f.y);
        float hw = exp(-abs(t.b * TL_HEIGHT_RANGE - world_z) / TL_HEIGHT_TOL);
        acc   += t.rg * (bw * hw);
        bilin += t.rg * bw;
        wsum  += bw * hw;
    }
    return wsum > 1e-4 ? acc / wsum : bilin;
}

vec3 sample_rc_fluence(vec3 normal, float world_z) {
    vec2 tex_size = vec2(textureSize(rc_fluence, 0));
    vec2 texel = gl_FragCoord.xy * 0.5;
    float fade = 1.0;
    if (normal.z < 0.5) {
        float z = max(world_z, 0.0);
        float halfres_px_per_iso_unit = tex_size.y * abs(projection[1][1]) * 0.5;
        texel.y += 12.5 * z * halfres_px_per_iso_unit + 3.0;
        fade = exp(-z * 1.2);
    }
    return texture(rc_fluence, texel / tex_size).rgb * fade;
}

float hex_dither(vec2 world_xy) {
    float sqrt3 = 1.7320508;
    float q = world_xy.x * (2.0 / 3.0);
    float r = -world_xy.x / 3.0 + world_xy.y / sqrt3;
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

#define FOREACH_CLUSTER_LIGHT(BODY) {                                          \
    uint cluster_idx = cluster_index();                                        \
    uvec2 grid_entry = light_grid[cluster_idx];                                \
    uint offset = grid_entry.x;                                                \
    uint count  = grid_entry.y;                                                \
    if (count == 0u && LIGHT_COUNT > 0u) {                                     \
        uint total = min(LIGHT_COUNT, 128u);                                   \
        for (uint i = 0u; i < total; ++i) {                                    \
            PointLight pl = point_lights[i];                                   \
            BODY                                                               \
        }                                                                      \
    } else {                                                                   \
        for (uint i = 0u; i < count; ++i) {                                    \
            PointLight pl = point_lights[global_light_indices[offset + i]];    \
            BODY                                                               \
        }                                                                      \
    }                                                                          \
}

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

vec3 apply_directional(vec3 color, vec3 normal, float sun_vis) {
    float diffuse = max(dot(normalize(normal), light_dir.xyz), 0.0);
    return color * (light_dir.w + diffuse * sun_vis * light_col.rgb);
}

vec3 apply_sky_ambient(vec3 lit, vec3 albedo, vec3 normal, float sky_vis) {
    return lit + albedo * star_light.rgb * star_light.a * sky_vis
                 * (0.5 + 0.5 * normal.z);
}

vec3 clustered_point_lighting(vec3 frag_pos, vec3 normal, vec3 base_color) {
    vec3 result = vec3(0.0);
    FOREACH_CLUSTER_LIGHT({
        result += apply_point_light(pl, frag_pos, normal, base_color);
    })
    return result;
}

#endif
