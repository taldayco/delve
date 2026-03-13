#version 450

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 3, binding = 0) uniform Uniforms {
    float time;
    float cam_x;
    float cam_y;
    float pad;
};

float hash11(uint n) {
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n ^= n >> 4u;
    n *= 0x27d4eb2du;
    n ^= n >> 15u;
    return float(n) / float(0xFFFFFFFFu);
}

float hash21(uvec2 p) {
    return hash11(p.x * 1664525u + p.y * 1013904223u + 12345u);
}


vec3 star_color_from_hash(uvec2 unc) {
    float t = hash21(unc * uvec2(53u, 97u));

    if (t < 0.08)       return vec3(0.60, 0.70, 1.00);
    else if (t < 0.20)  return vec3(0.80, 0.88, 1.00);
    else if (t < 0.45)  return vec3(1.00, 1.00, 0.96);
    else if (t < 0.65)  return vec3(1.00, 0.95, 0.70);
    else if (t < 0.82)  return vec3(1.00, 0.75, 0.40);
    else                return vec3(1.00, 0.45, 0.25);
}

float star_layer(vec2 frag_px, float t, float cell_px, float chance, float radius_scale,
                 inout vec3 accumulated_color) {
    vec2  cell = floor(frag_px / cell_px);
    float brightness = 0.0;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2  nc  = cell + vec2(dx, dy);
            uvec2 unc = uvec2(int(nc.x) + 8192, int(nc.y) + 8192);

            if (hash21(unc * uvec2(7u, 13u)) > chance) continue;

            float sx   = hash21(unc * uvec2(3u,  17u)) * cell_px;
            float sy   = hash21(unc * uvec2(11u,  5u)) * cell_px;
            float freq = 0.5 + hash21(unc * uvec2(19u, 23u)) * 2.0;
            float phase= hash21(unc * uvec2(29u, 31u)) * 6.2831853;
            float twinkle = 0.65 + 0.35 * sin(t * freq + phase);
            float lum  = 0.25 + hash21(unc * uvec2(37u, 41u)) * 0.75;

            vec2  star_world = nc * cell_px + vec2(sx, sy);
            float dist = length(frag_px - star_world);


            float r_core = (0.3 + lum * 0.4) * radius_scale;
            float r_glow = r_core * 3.5;
            float core   = exp(-dist * dist / (r_core * r_core + 0.0001));
            float glow   = exp(-dist * dist / (r_glow * r_glow + 0.0001)) * 0.35;
            float contrib = (core + glow) * lum * twinkle;

            vec3 sc = star_color_from_hash(unc);
            accumulated_color += sc * contrib;
            brightness += contrib;
        }
    }
    return clamp(brightness, 0.0, 1.0);
}

void main() {
    const float PARALLAX = 0.18;
    vec2 pan = vec2(cam_x, cam_y) * PARALLAX;

    vec2 px  = frag_uv * 2048.0 + pan;

    vec3 color = vec3(0.0);


    star_layer(px, time, 120.0, 0.45, 1.5, color);

    star_layer(px, time,  55.0, 0.38, 0.9, color);

    star_layer(px, time,  28.0, 0.30, 0.5, color);

    out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
