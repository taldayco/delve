#version 450

#include "coord.glsl"

layout(location = 0) in vec3  in_pos;
layout(location = 1) in vec3  in_normal;
layout(location = 2) in float in_bone_index;
layout(location = 3) in float in_bone_weight;
layout(location = 4) in float in_bone_index2;

layout(set = 1, binding = 1) uniform BoneMatrices {
    mat4 bones[18];
};

layout(location = 0) out vec3  frag_world_pos;
layout(location = 1) out vec3  frag_normal;
layout(location = 2) out float frag_bone_index;

void main() {
    uint b1 = min(uint(in_bone_index),  17u);
    uint b2 = min(uint(in_bone_index2), 17u);
    float w1 = clamp(in_bone_weight, 0.0, 1.0);
    float w2 = 1.0 - w1;

    vec4 p1 = bones[b1] * vec4(in_pos, 1.0);
    vec4 p2 = bones[b2] * vec4(in_pos, 1.0);
    vec4 world_pos = p1 * w1 + p2 * w2;

    gl_Position    = projection * view * world_pos;
    frag_world_pos = world_pos.xyz;
    frag_normal    = normalize(mat3(bones[b1]) * in_normal * w1 +
                               mat3(bones[b2]) * in_normal * w2);
    frag_bone_index = in_bone_index;
}
