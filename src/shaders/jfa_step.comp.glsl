#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D jfa_in;
layout(set = 1, binding = 0, rg16f) uniform writeonly image2D jfa_out;

layout(set = 2, binding = 0) uniform Params {
    int jump;
    int rt_w;
    int rt_h;
    int _pad0;
};

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= rt_w || p.y >= rt_h) return;

    vec2  best_seed = vec2(-1.0);
    float best_d    = 1e30;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 q = p + ivec2(dx, dy) * jump;
            if (q.x < 0 || q.y < 0 || q.x >= rt_w || q.y >= rt_h) continue;
            vec2 seed = texelFetch(jfa_in, q, 0).rg;
            if (seed.x < 0.0) continue;
            float d = distance(vec2(p), seed);
            if (d < best_d) {
                best_d    = d;
                best_seed = seed;
            }
        }
    }
    imageStore(jfa_out, p, vec4(best_seed, 0.0, 0.0));
}
