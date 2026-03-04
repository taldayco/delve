#version 450

#include "lighting_common.glsl"
#include "coord.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

layout(set = 1, binding = 0) uniform ActorUniforms {
    vec4 bone_color_pad; // rgb = bone_color, a = unused
};

void main() {
    vec3 base  = bone_color_pad.rgb;
    vec3 N     = normalize(frag_normal);

    // Directional diffuse
    float diffuse = max(dot(N, light_dir.xyz), 0.0);
    vec3  lit     = base * (light_dir.w + diffuse * light_col.rgb);

    // Star ambient
    float NdotUp  = max(N.z, 0.0);
    lit += base * star_light.rgb * star_light.w * NdotUp;

    out_color = vec4(clamp(lit, 0.0, 1.0), 1.0);
}
