#version 450

layout(location = 3) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

void main() {
    float wall = frag_normal.z > 0.5 ? 0.0 : 1.0;
    out_color = vec4(0.0, 0.0, 0.0, wall);
}
