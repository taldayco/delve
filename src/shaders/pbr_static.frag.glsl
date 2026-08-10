#version 450

#define COORD_FRAGMENT_STAGE
#include "coord.glsl"
#include "pbr_common.glsl"
#include "lighting_common.glsl"
#include "tone.glsl"

layout(location = 0) in vec3  frag_color;
layout(location = 1) in vec3  frag_world_pos;
layout(location = 2) in float frag_sheen;
layout(location = 3) in vec3  frag_normal;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 N = normalize(frag_normal);

    vec3 V = normalize(view_dir_ws.xyz);

    float dither = hex_dither(frag_world_pos.xy);
    vec3 albedo = to_linear(clamp(frag_color * (1.0 + dither), 0.0, 1.0));

    float roughness = mix(0.9, 0.3, frag_sheen);
    float metallic  = frag_sheen * 0.1;

    vec2 tl = sample_terrain_light(frag_world_pos.xy);

    vec3 L_dir = normalize(light_dir.xyz);
    vec3 dir_radiance = light_col.rgb;
    vec3 color = cook_torrance_brdf(albedo, metallic, roughness, N, V, L_dir, dir_radiance) * tl.r;

    color += albedo * light_dir.w;

    FOREACH_CLUSTER_LIGHT({
        vec3 to_light = pl.positionRadius.xyz - frag_world_pos;
        float dist = length(to_light);
        float radius = pl.positionRadius.w;
        if (dist < radius) {
            float atten = 1.0 - clamp(dist / radius, 0.0, 1.0);
            atten = atten * atten;

            vec3 L = normalize(to_light);
            vec3 radiance = pl.colorIntensity.rgb * pl.colorIntensity.w * atten;
            color += cook_torrance_brdf(albedo, metallic, roughness, N, V, L, radiance);
        }
    })

    color = apply_sky_ambient(color, albedo, N, tl.g);

    out_color = vec4(encode_output_exp(color, EXPOSURE), 1.0);
}
