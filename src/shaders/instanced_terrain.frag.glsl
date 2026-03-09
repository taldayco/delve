#version 450

#define COORD_FRAGMENT_STAGE
#include "coord.glsl"

layout(location = 0) in vec3  frag_color;
layout(location = 1) in vec3  frag_world_pos;
layout(location = 2) in float frag_sheen;
layout(location = 3) in vec3  frag_normal;

layout(location = 0) out vec4 out_color;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 2, binding = 0) readonly buffer LightBuffer {
    PointLight point_lights[];
};

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

float hex_dither(vec2 world_xy) {
    float sqrt3 = 1.7320508;
    float q = world_xy.x / sqrt3;
    float r = world_xy.y - q * 0.5;
    int iq = int(round(q));
    int ir = int(round(r));
    int is = int(round(-q - r));
    float dq = abs(float(iq) - q);
    float dr = abs(float(ir) - r);
    float ds = abs(float(is) - (-q - r));
    if (dq > dr && dq > ds) iq = -ir - is;
    else if (dr > ds)        ir = -iq - is;
    uint hash = uint(iq) * 374761393u ^ uint(ir) * 668265263u;
    return (float(hash & 0xFFu) / 255.0 - 0.5) * 0.25;
}

void main() {
    float dither   = hex_dither(frag_world_pos.xy);
    vec3  dithered = clamp(frag_color * (1.0 + dither), 0.0, 1.0);

    vec3 lit = apply_directional(dithered, frag_normal);

    uint count = LIGHT_COUNT;
    for (uint i = 0; i < count && i < 128u; ++i) {
        lit += apply_point_light(point_lights[i], frag_world_pos, frag_normal, dithered);
    }

    vec3 final_color = apply_star_ambient(lit, frag_normal, frag_sheen);
    out_color = vec4(clamp(final_color, 0.0, 1.0), 1.0);
}
