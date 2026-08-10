#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D sdf_tex;
layout(set = 0, binding = 1) uniform sampler2D capture_tex;
layout(set = 0, binding = 2) uniform sampler2D parent_atlas;
layout(set = 1, binding = 0, rgba16f) uniform writeonly image2D atlas_out;

layout(set = 2, binding = 0) uniform Params {
    int   cascade_index;
    int   dirs;
    int   dirs_sqrt;
    int   spacing;
    float t_start;
    float t_len;
    int   atlas_w;
    int   atlas_h;
    int   rt_w;
    int   rt_h;
    int   is_top;
    int   _pad0;
};

const float PI        = 3.14159265358979;
const float HIT_EPS   = 0.5;
const int   MAX_STEPS = 64;

vec4 march(vec2 origin, vec2 dir) {
    vec2  inv_rt = 1.0 / vec2(rt_w, rt_h);
    float t      = t_start;
    float t_end  = t_start + t_len;
    for (int s = 0; s < MAX_STEPS && t < t_end; ++s) {
        vec2 pos = origin + dir * t;
        if (pos.x < 0.0 || pos.y < 0.0 ||
            pos.x >= float(rt_w) || pos.y >= float(rt_h))
            return vec4(0.0, 0.0, 0.0, 1.0);
        float d = texture(sdf_tex, pos * inv_rt).r;
        if (d < HIT_EPS)
            return vec4(texture(capture_tex, pos * inv_rt).rgb, 0.0);
        t += max(d, HIT_EPS);
    }

    return vec4(0.0, 0.0, 0.0, t >= t_end ? 1.0 : 0.0);
}

vec4 parent_fetch(ivec2 probe, int pd, int p_dirs_sqrt, ivec2 p_probes) {
    ivec2 max_probe = min(p_probes, ivec2(atlas_w, atlas_h) / p_dirs_sqrt) - 1;
    probe = clamp(probe, ivec2(0), max_probe);
    ivec2 texel = probe * p_dirs_sqrt + ivec2(pd % p_dirs_sqrt, pd / p_dirs_sqrt);
    return texelFetch(parent_atlas, texel, 0);
}

void main() {
    ivec2 tid    = ivec2(gl_GlobalInvocationID.xy);
    ivec2 probes = ivec2((rt_w + spacing - 1) / spacing,
                         (rt_h + spacing - 1) / spacing);
    ivec2 probe  = tid / dirs_sqrt;
    ivec2 dxy    = tid % dirs_sqrt;
    int   d      = dxy.y * dirs_sqrt + dxy.x;
    if (probe.x >= probes.x || probe.y >= probes.y ||
        tid.x >= atlas_w || tid.y >= atlas_h) return;

    vec2  origin = (vec2(probe) + 0.5) * float(spacing);
    float angle  = 2.0 * PI * (float(d) + 0.5) / float(dirs);
    vec4  near   = march(origin, vec2(cos(angle), sin(angle)));

    vec4 result = near;
    if (is_top == 0) {

        int   p_spacing   = spacing * 2;
        int   p_dirs_sqrt = dirs_sqrt * 2;
        ivec2 p_probes    = ivec2((rt_w + p_spacing - 1) / p_spacing,
                                  (rt_h + p_spacing - 1) / p_spacing);
        vec2  f    = origin / float(p_spacing) - 0.5;
        ivec2 base = ivec2(floor(f));
        vec2  frac = f - vec2(base);

        vec4 far = vec4(0.0);
        for (int k = 0; k < 4; ++k) {
            int  pd  = d * 4 + k;
            vec4 v00 = parent_fetch(base,               pd, p_dirs_sqrt, p_probes);
            vec4 v10 = parent_fetch(base + ivec2(1, 0), pd, p_dirs_sqrt, p_probes);
            vec4 v01 = parent_fetch(base + ivec2(0, 1), pd, p_dirs_sqrt, p_probes);
            vec4 v11 = parent_fetch(base + ivec2(1, 1), pd, p_dirs_sqrt, p_probes);
            far += mix(mix(v00, v10, frac.x), mix(v01, v11, frac.x), frac.y);
        }
        far *= 0.25;
        result = vec4(near.rgb + near.a * far.rgb, near.a * far.a);
    }
    imageStore(atlas_out, tid, result);
}
