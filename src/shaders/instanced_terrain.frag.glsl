#version 450

#define COORD_FRAGMENT_STAGE
#include "coord.glsl"
#include "lighting_common.glsl"

layout(location = 0) in vec3  frag_color;
layout(location = 1) in vec3  frag_world_pos;
layout(location = 2) in float frag_sheen;
layout(location = 3) in vec3  frag_normal;

layout(location = 0) out vec4 out_color;

void main() {
    float dither   = hex_dither(frag_world_pos.xy);
    vec3  dithered = clamp(frag_color * (1.0 + dither), 0.0, 1.0);

    vec3 lit = apply_directional(dithered, frag_normal);
    lit += clustered_point_lighting(frag_world_pos, frag_normal, dithered);

    vec3 final_color = apply_star_ambient(lit, frag_normal, frag_sheen);
    out_color = vec4(clamp(final_color, 0.0, 1.0), 1.0);
}
