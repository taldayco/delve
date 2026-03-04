#version 450

#include "lighting_common.glsl"
#include "coord.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PushConstants {
    vec4 color;
    int  wireframe;
};

void main() {
    if (wireframe != 0) {
        out_color = vec4(1.0);
        return;
    }

    vec3 base = color.rgb;
    vec3 N    = normalize(frag_normal);

    float diffuse = max(dot(N, light_dir.xyz), 0.0);
    vec3  lit     = base * (light_dir.w + diffuse * light_col.rgb);

    float NdotUp = max(N.z, 0.0);
    lit += base * star_light.rgb * star_light.w * NdotUp;

    out_color = vec4(clamp(lit, 0.0, 1.0), 1.0);
}