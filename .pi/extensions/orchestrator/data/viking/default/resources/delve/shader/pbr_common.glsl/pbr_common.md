// PBR Cook-Torrance BRDF utilities
// Included by pbr_static.frag.glsl and pbr_skinned.vert.glsl

#ifndef PBR_COMMON_GLSL
#define PBR_COMMON_GLSL

const float PI = 3.14159265359;

// GGX/Trowbridge-Reitz normal distribution function
float distribution_ggx(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Single-direction Schlick-GGX geometry function
float geometry_schlick_ggx(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's geometry function (combined view + light)
float geometry_smith(float NdotV, float NdotL, float roughness) {
    return geometry_schlick_ggx(NdotV, roughness) *
           geometry_schlick_ggx(NdotL, roughness);
}

// Schlick Fresnel approximation
vec3 fresnel_schlick(float cos_theta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

// Full Cook-Torrance BRDF evaluation for a single light
// Returns final outgoing radiance contribution
vec3 cook_torrance_brdf(vec3 albedo, float metallic, float roughness,
                         vec3 N, vec3 V, vec3 L, vec3 light_radiance) {
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    if (NdotL <= 0.0) return vec3(0.0);

    // Dielectric F0 = 0.04, metallic lerps to albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = distribution_ggx(NdotH, roughness);
    float G = geometry_smith(NdotV, NdotL, roughness);
    vec3  F = fresnel_schlick(HdotV, F0);

    // Specular term
    vec3 numerator   = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular     = numerator / denominator;

    // Energy conservation: diffuse = (1 - Fresnel) * (1 - metallic)
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / PI + specular) * light_radiance * NdotL;
}

#endif // PBR_COMMON_GLSL
