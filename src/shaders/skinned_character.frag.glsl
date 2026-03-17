#version 450

#define COORD_FRAGMENT_STAGE
#include "coord.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_texcoord;

layout(location = 0) out vec4 out_color;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 2, binding = 0) readonly buffer LightBuffer {
    PointLight point_lights[];
};

void main() {
    // Wireframe: solid color with subtle depth fade
    vec3 wire_color = vec3(0.7, 0.85, 0.95);
    out_color = vec4(wire_color, 1.0);
}
