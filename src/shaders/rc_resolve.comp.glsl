#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D cascade0_atlas;
layout(set = 1, binding = 0, rgba16f) uniform writeonly image2D fluence_out;

layout(set = 2, binding = 0) uniform Params {
    int rt_w;
    int rt_h;
    int _pad0;
    int _pad1;
};

const float PI        = 3.14159265358979;
const float K_FLUENCE = 1.0;

vec3 probe_avg(ivec2 probe, ivec2 probes) {
    probe = clamp(probe, ivec2(0), probes - 1);
    ivec2 base = probe * 2;
    vec3 sum = texelFetch(cascade0_atlas, base,               0).rgb
             + texelFetch(cascade0_atlas, base + ivec2(1, 0), 0).rgb
             + texelFetch(cascade0_atlas, base + ivec2(0, 1), 0).rgb
             + texelFetch(cascade0_atlas, base + ivec2(1, 1), 0).rgb;
    return sum * 0.25;
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= rt_w || p.y >= rt_h) return;

    ivec2 probes = ivec2((rt_w + 1) / 2, (rt_h + 1) / 2);
    vec2  f      = (vec2(p) + 0.5) / 2.0 - 0.5;
    ivec2 base   = ivec2(floor(f));
    vec2  frac   = f - vec2(base);

    vec3 v00 = probe_avg(base,               probes);
    vec3 v10 = probe_avg(base + ivec2(1, 0), probes);
    vec3 v01 = probe_avg(base + ivec2(0, 1), probes);
    vec3 v11 = probe_avg(base + ivec2(1, 1), probes);
    vec3 rad = mix(mix(v00, v10, frac.x), mix(v01, v11, frac.x), frac.y);

    imageStore(fluence_out, p, vec4(rad * (2.0 * PI / 4.0) * K_FLUENCE, 1.0));
}
