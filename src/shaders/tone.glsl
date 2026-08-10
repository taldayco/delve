#ifndef TONE_GLSL
#define TONE_GLSL
const float LIGHT_EXPOSURE = 1.6;
vec3 to_linear(vec3 c)  { return pow(max(c, vec3(0.0)), vec3(2.2)); }
// Luminance-based Reinhard: compresses brightness while preserving hue,
// so saturated lava stays orange instead of drifting toward yellow-white.
vec3 encode_output_exp(vec3 c, float exposure) {
    c *= exposure;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = c / (1.0 + lum);
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}
vec3 encode_output(vec3 c) { return encode_output_exp(c, LIGHT_EXPOSURE); }
#endif
