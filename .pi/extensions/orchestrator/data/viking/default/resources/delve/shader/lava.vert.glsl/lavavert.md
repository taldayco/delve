#version 450

#include "coord.glsl"

layout(location = 0) in vec3  in_pos;
layout(location = 1) in float in_time_offset;

layout(location = 0) out vec3 frag_color;

void main() {
    float t     = TIME + in_time_offset;
    float wave1 = sin(in_pos.x * 0.3  + t)                   * 0.02;
    float wave2 = sin(in_pos.y * 0.21 + t * 1.3)             * 0.015;
    float wave3 = sin((in_pos.x + in_pos.y) * 0.15 + t * 0.8) * 0.01;
    float z     = in_pos.z + wave1 + wave2 + wave3;

    gl_Position = projection * view * vec4(in_pos.xy, z - 0.002, 1.0);

    float depth_factor = clamp(0.9 + (z - in_pos.z) * 2.0, 0.8, 1.1);
    frag_color = lava_color.rgb * depth_factor;
}
