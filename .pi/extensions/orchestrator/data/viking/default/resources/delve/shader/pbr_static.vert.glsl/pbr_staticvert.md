#version 450

#include "coord.glsl"

layout(location = 0) in vec3  in_pos;
layout(location = 1) in vec3  in_color;
layout(location = 2) in float in_sheen;
layout(location = 3) in vec3  in_normal;

layout(location = 0) out vec3  frag_color;
layout(location = 1) out vec3  frag_world_pos;
layout(location = 2) out float frag_sheen;
layout(location = 3) out vec3  frag_normal;

void main() {
    gl_Position   = projection * view * vec4(in_pos, 1.0);
    frag_color     = in_color;
    frag_world_pos = in_pos;
    frag_sheen     = in_sheen;
    frag_normal    = in_normal;
}
