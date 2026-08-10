#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D capture_tex;
layout(set = 1, binding = 0, rg16f) uniform writeonly image2D jfa_out;

void main() {
    ivec2 p    = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(jfa_out);
    if (p.x >= size.x || p.y >= size.y) return;

    float coverage = texelFetch(capture_tex, p, 0).a;
    vec2  seed     = coverage > 0.5 ? vec2(p) : vec2(-1.0);
    imageStore(jfa_out, p, vec4(seed, 0.0, 0.0));
}
