#version 450

#include "tone.glsl"

layout(location = 0) in vec3 frag_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(encode_output(to_linear(frag_color) * 2.0), 1.0);
}
