#version 450

layout(location = 0) in vec3 frag_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(pow(max(frag_color, vec3(0.0)), vec3(2.2)) * 2.0, 1.0);
}
