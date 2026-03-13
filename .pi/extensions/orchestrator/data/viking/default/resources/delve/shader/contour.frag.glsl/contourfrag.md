#version 450

layout(location = 0) in float frag_opacity;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(0.867, 0.867, 0.867, frag_opacity);
}
