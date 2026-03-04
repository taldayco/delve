#version 450

#include "coord.glsl"

layout(location = 0) in vec3   inPosition;
layout(location = 1) in vec3   inNormal;
layout(location = 2) in ivec2  inBoneIndex;
layout(location = 3) in float  inBoneWeight;

layout(set = 2, binding = 0) uniform BoneMatrices {
    mat4 bone_matrices[64];
    mat4 view_proj;
};

layout(location = 0) out vec3 frag_world_pos;
layout(location = 1) out vec3 frag_normal;

void main() {
    uint b0 = min(uint(inBoneIndex.x), 63u);
    uint b1 = min(uint(inBoneIndex.y), 63u);
    float w  = clamp(inBoneWeight, 0.0, 1.0);
    float w2 = 1.0 - w;

    vec4 world_pos = w  * bone_matrices[b0] * vec4(inPosition, 1.0)
                   + w2 * bone_matrices[b1] * vec4(inPosition, 1.0);

    mat3 inv_trans0 = transpose(inverse(mat3(bone_matrices[b0])));
    mat3 inv_trans1 = transpose(inverse(mat3(bone_matrices[b1])));
    vec3 world_normal = normalize(inv_trans0 * inNormal * w + inv_trans1 * inNormal * w2);

    gl_Position    = projection * view * world_pos;
    frag_world_pos = world_pos.xyz;
    frag_normal    = world_normal;
}