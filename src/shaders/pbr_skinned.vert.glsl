#version 450

#include "coord.glsl"

layout(set = 0, binding = 0) readonly buffer JointBuffer {
    mat4 joint_matrices[];  // skinning_palette from Animator
};

// SkinnedVertex inputs (72 bytes)
layout(location = 0) in vec3  in_pos;
layout(location = 1) in vec3  in_normal;
layout(location = 2) in vec2  in_uv;
layout(location = 3) in vec3  in_color;
layout(location = 4) in float in_roughness;
layout(location = 5) in float in_metallic;
layout(location = 6) in uvec4 in_joint_indices;
layout(location = 7) in vec4  in_weights;

layout(location = 0) out vec3  frag_color;
layout(location = 1) out vec3  frag_world_pos;
layout(location = 2) out float frag_sheen;
layout(location = 3) out vec3  frag_normal;
layout(location = 4) out vec2  frag_uv;
layout(location = 5) out float frag_roughness;
layout(location = 6) out float frag_metallic;

void main() {
    // 4-bone linear blend skinning
    mat4 skin_mat = joint_matrices[in_joint_indices.x] * in_weights.x
                  + joint_matrices[in_joint_indices.y] * in_weights.y
                  + joint_matrices[in_joint_indices.z] * in_weights.z
                  + joint_matrices[in_joint_indices.w] * in_weights.w;

    vec4 world_pos = skin_mat * vec4(in_pos, 1.0);
    gl_Position    = projection * view * world_pos;

    mat3 normal_mat = transpose(inverse(mat3(skin_mat)));

    frag_color     = in_color;
    frag_world_pos = world_pos.xyz;
    frag_sheen     = 1.0;
    frag_normal    = normalize(normal_mat * in_normal);
    frag_uv        = in_uv;
    frag_roughness = in_roughness;
    frag_metallic  = in_metallic;
}
