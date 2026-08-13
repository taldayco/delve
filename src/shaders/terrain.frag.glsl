#version 450

#define COORD_FRAGMENT_STAGE
#include "coord.glsl"
#include "lighting_common.glsl"
#include "tone.glsl"

layout(location = 0) in vec3  frag_color;
layout(location = 1) in vec3  frag_world_pos;
layout(location = 2) in float frag_sheen;
layout(location = 3) in vec3  frag_normal;

layout(location = 0) out vec4 out_color;

void main() {
    float dither   = hex_dither(frag_world_pos.xy - frag_normal.xy * 0.05);
    vec3  dithered = to_linear(clamp(frag_color * (1.0 + dither), 0.0, 1.0));

    vec2 tl = sample_terrain_light(frag_world_pos.xy, frag_world_pos.z);
    vec3 lit = apply_directional(dithered, frag_normal, tl.r);
    lit += clustered_point_lighting(frag_world_pos, frag_normal, dithered);
    lit += dithered * sample_rc_fluence(frag_normal, frag_world_pos.z) * RC_INTENSITY;

    vec3 final_color = apply_sky_ambient(lit, dithered, frag_normal, tl.g);
    out_color = vec4(encode_output_exp(final_color, EXPOSURE), 1.0);
}
