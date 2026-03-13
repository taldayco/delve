#version 450

#include "coord.glsl"

layout(location = 0) in vec3 in_pos;

layout(location = 0) out float frag_opacity;

void main() {
    gl_Position  = projection * view * vec4(in_pos, 1.0);
    frag_opacity = params1.y;
}
