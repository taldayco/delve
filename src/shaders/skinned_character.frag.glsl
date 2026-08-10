#version 450

#define COORD_FRAGMENT_STAGE
#include "coord.glsl"
#include "lighting_common.glsl"
#include "tone.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_texcoord;
layout(location = 3) in float frag_sheen; // unused since sky-ambient bake; kept so the vertex/fragment interface stays matched

layout(location = 0) out vec4 out_color;

void main() {
    float dither = hex_dither(frag_world_pos.xy);
    vec3 base_color = to_linear(clamp(vec3(0.6, 0.7, 0.8) * (1.0 + dither), 0.0, 1.0));

    vec3 N = normalize(frag_normal);
    vec2 tl = sample_terrain_light(frag_world_pos.xy);
    vec3 lit = apply_directional(base_color, N, tl.r);
    lit += clustered_point_lighting(frag_world_pos, N, base_color);

    vec3 final_color = apply_sky_ambient(lit, base_color, N, tl.g);
    out_color = vec4(encode_output_exp(final_color, EXPOSURE), 1.0);
}
