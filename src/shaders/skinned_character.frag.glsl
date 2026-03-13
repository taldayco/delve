#version 450

#define COORD_FRAGMENT_STAGE
#include "coord.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_texcoord;

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

void main() {
    vec3 base_color = vec3(0.6, 0.7, 0.8);
    const float sheen = 0.4;

    vec3 lit = apply_directional(base_color, frag_normal);

    uint count = LIGHT_COUNT;
    for (uint i = 0; i < count && i < 128u; ++i) {
        lit += apply_point_light(point_lights[i], frag_world_pos, frag_normal, base_color);
    }

    vec3 final_color = apply_star_ambient(lit, frag_normal, sheen);
    out_color = vec4(clamp(final_color, 0.0, 1.0), 1.0);
}