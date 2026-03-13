#version 450

#include "coord.glsl"

layout(location = 0) in vec3  in_pos;
layout(location = 1) in vec3  in_normal;
layout(location = 2) in vec2  in_texcoord;
layout(location = 3) in vec4  in_tangent;
layout(location = 4) in uvec4 in_joints;
layout(location = 5) in vec4  in_weights;

// Bone palette: each mat4 already encodes root_transform * global[i] * inverse_bind[i].
// No separate model matrix push constant needed.
layout(set = 0, binding = 0) readonly buffer BoneBuffer { mat4 bones[65]; };

layout(location = 0) out vec3 frag_world_pos;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_texcoord;

void main() {
    mat4 skin_mat =
        in_weights.x * bones[in_joints.x] +
        in_weights.y * bones[in_joints.y] +
        in_weights.z * bones[in_joints.z] +
        in_weights.w * bones[in_joints.w];

    vec4 world_pos = skin_mat * vec4(in_pos, 1.0);
    gl_Position    = projection * view * world_pos;

    frag_world_pos = world_pos.xyz;
    frag_normal    = normalize(mat3(skin_mat) * in_normal);
    frag_texcoord  = in_texcoord;
}
