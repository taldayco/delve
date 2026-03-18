#version 450

#define COORD_FRAGMENT_STAGE
#include "coord.glsl"
#include "lighting_common.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_texcoord;
layout(location = 3) in float frag_sheen;

layout(location = 0) out vec4 out_color;

void main() {
    float dither = hex_dither(frag_world_pos.xy);
    vec3 base_color = clamp(vec3(0.6, 0.7, 0.8) * (1.0 + dither), 0.0, 1.0);

    vec3 N = normalize(frag_normal);
    vec3 lit = apply_directional(base_color, N);
    lit += clustered_point_lighting(frag_world_pos, N, base_color);

    vec3 final_color = apply_star_ambient(lit, N, frag_sheen);
    out_color = vec4(clamp(final_color, 0.0, 1.0), 1.0);
}
