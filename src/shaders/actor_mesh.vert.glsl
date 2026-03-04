#version 450

#include "coord.glsl"

layout(location = 0) in vec3  in_position;
layout(location = 1) in vec3  in_normal;
layout(location = 2) in ivec2 in_bone_indices;
layout(location = 3) in float in_bone_weight;

layout(set = 2, binding = 0) uniform BoneMatrices {
    mat4 bone_matrices[32];
};

layout(location = 0) out vec3 frag_world_pos;
layout(location = 1) out vec3 frag_normal;

void main() {
    uint b0 = min(uint(in_bone_indices.x), 31u);
    uint b1 = min(uint(in_bone_indices.y), 31u);
    float w  = clamp(in_bone_weight, 0.0, 1.0);
    float w2 = 1.0 - w;

    vec4 world_pos = w  * bone_matrices[b0] * vec4(in_position, 1.0)
                   + w2 * bone_matrices[b1] * vec4(in_position, 1.0);

    mat3 inv_trans0 = transpose(inverse(mat3(bone_matrices[b0])));
    mat3 inv_trans1 = transpose(inverse(mat3(bone_matrices[b1])));
    vec3 world_normal = normalize(inv_trans0 * in_normal * w + inv_trans1 * in_normal * w2);

    gl_Position    = projection * view * world_pos;
    frag_world_pos = world_pos.xyz;
    frag_normal    = world_normal;
}
