#version 450

// Input from vertex shader
layout(location = 0) in vec4 frag_color;

// Output color
layout(location = 0) out vec4 out_color;

void main() {
    out_color = frag_color;
}
