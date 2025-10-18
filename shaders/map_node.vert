#version 450

// Vertex attributes (circle geometry)
layout(location = 0) in vec2 in_position;

// Instance attributes (per-node data)
layout(location = 1) in vec2 instance_position;
layout(location = 2) in vec4 instance_color;
layout(location = 3) in float instance_scale;

// Push constants (camera transform)
layout(push_constant) uniform PushConstants {
    mat4 view_projection;
} push;

// Output to fragment shader
layout(location = 0) out vec4 frag_color;

void main() {
    // Apply instance scale and position to vertex
    vec2 world_pos = in_position * instance_scale + instance_position;
    
    // Transform to clip space
    gl_Position = push.view_projection * vec4(world_pos, 0.0, 1.0);
    
    // Pass color to fragment shader
    frag_color = instance_color;
}
