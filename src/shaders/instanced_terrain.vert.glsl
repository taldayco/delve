#version 450

#include "coord.glsl"

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;

struct ColumnInstance { mat4 model; vec4 color; };
layout(set = 0, binding = 0) readonly buffer InstanceBuffer {
    ColumnInstance instances[];
};

layout(location = 0) out vec3  frag_color;
layout(location = 1) out vec3  frag_world_pos;
layout(location = 2) out float frag_sheen;
layout(location = 3) out vec3  frag_normal;

void main() {
    ColumnInstance inst = instances[gl_InstanceIndex];
    vec4 world_pos = inst.model * vec4(in_pos, 1.0);
    gl_Position    = projection * view * world_pos;
    frag_color     = inst.color.rgb;
    frag_world_pos = world_pos.xyz;
    frag_sheen     = 1.0;
    mat3 normal_mat = transpose(inverse(mat3(inst.model)));
    frag_normal     = normalize(normal_mat * in_normal);
}
