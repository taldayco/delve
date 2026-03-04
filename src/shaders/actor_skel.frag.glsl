#version 450

layout(location = 0) in vec3  frag_world_pos;
layout(location = 1) in vec3  frag_normal;
layout(location = 2) in float frag_bone_index;

layout(location = 0) out vec4 out_color;

layout(set = 1, binding = 0) uniform BoneColors {
    vec4 per_bone_color[18];
};

void main() {
    uint bi   = min(uint(frag_bone_index), 17u);
    vec3 base = per_bone_color[bi].rgb;

    vec3  light_dir = normalize(vec3(0.5, 0.5, 1.0));
    float diffuse   = max(dot(normalize(frag_normal), light_dir), 0.0);
    vec3  lit       = base * (0.35 + 0.65 * diffuse);

    out_color = vec4(clamp(lit, 0.0, 1.0), 1.0);
}
