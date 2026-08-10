#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D jfa_in;
layout(set = 1, binding = 0, r16f) uniform writeonly image2D sdf_out;

void main() {
    ivec2 p    = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(sdf_out);
    if (p.x >= size.x || p.y >= size.y) return;

    vec2  seed = texelFetch(jfa_in, p, 0).rg;
    float d    = seed.x < 0.0 ? 1e4 : distance(vec2(p), seed);
    imageStore(sdf_out, p, vec4(d, 0.0, 0.0, 0.0));
}
