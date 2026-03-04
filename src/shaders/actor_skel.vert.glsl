#version 450

// Skeleton mesh vertex shader — CPU LBS path.
// deform_skeleton_mesh() has already written world-space positions into the
// vertex buffer.  This shader only applies the camera MVP transform.
// No bone matrices needed; bone_index0 is passed to frag for per-bone coloring.

#include "coord.glsl"

layout(location = 0) in vec3  in_pos;
layout(location = 1) in vec3  in_normal;
layout(location = 2) in float in_bone_index0;
layout(location = 3) in float in_bone_weight;
layout(location = 4) in float in_bone_index1;

layout(location = 0) out vec3  frag_world_pos;
layout(location = 1) out vec3  frag_normal;
layout(location = 2) out float frag_bone_index;

void main() {
    vec4 world_pos = vec4(in_pos, 1.0);
    gl_Position    = projection * view * world_pos;
    frag_world_pos = in_pos;
    frag_normal    = normalize(in_normal);
    frag_bone_index = in_bone_index0;
}
