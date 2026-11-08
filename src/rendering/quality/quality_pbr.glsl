#ifndef C4_QUALITY_PBR_GLSL
#define C4_QUALITY_PBR_GLSL

// Enhanced-only native metallic/roughness evaluators; provenance/override and
// source-light gates live in callers. Original keeps its source lighting path.
const float c4_pbr_pi = 3.14159265358979323846;

vec3 c4_pbr_unit(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1.0e-12));
}

float c4_pbr_srgb_channel(float value) {
    return value <= 0.04045 ? value / 12.92
                          : pow((value + 0.055) / 1.055, 2.4);
}

vec3 c4_quality_srgb_to_linear(vec3 source) {
    return vec3(c4_pbr_srgb_channel(source.r),
                c4_pbr_srgb_channel(source.g),
                c4_pbr_srgb_channel(source.b));
}

vec3 c4_pbr_schlick(vec3 sourceSpecular, float cosine) {
    float grazing = 1.0 - clamp(cosine, 0.0, 1.0);
    float grazing2 = grazing * grazing;
    return sourceSpecular + (vec3(1.0) - sourceSpecular)
         * (grazing2 * grazing2 * grazing);
}

// Exact Smith GGX masking term, not the direct-light Schlick approximation.
float c4_pbr_smith(float cosine, float alphaSquared) {
    return (2.0 * cosine) / max(cosine +
        sqrt(alphaSquared + (1.0 - alphaSquared) * cosine * cosine), 1.0e-6);
}

vec3 c4_pbr_direct(vec3 sourceAlbedoSrgb, vec3 sourceSpecular,
                   float roughness, float metallic, float sourceMask, vec3 normal,
                   vec3 viewDirection, vec3 lightDirection,
                   vec3 lightRadianceLinear) {
    vec3 n = c4_pbr_unit(normal);
    vec3 v = c4_pbr_unit(viewDirection);
    vec3 l = c4_pbr_unit(lightDirection);
    float nv = max(dot(n, v), 0.0);
    float nl = max(dot(n, l), 0.0);
    if (nv <= 0.0 || nl <= 0.0) return vec3(0.0);
    vec3 h = c4_pbr_unit(v + l);
    float nh = max(dot(n, h), 0.0);
    roughness = clamp(roughness, 0.045, 1.0);
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = nh * nh * (alphaSquared - 1.0) + 1.0;
    float distribution = alphaSquared /
        max(c4_pbr_pi * denominator * denominator, 1.0e-12);
    vec3 fresnel = c4_pbr_schlick(clamp(sourceSpecular, 0.0, 1.0),
                                max(dot(v, h), 0.0)) * clamp(sourceMask, 0.0, 1.0);
    float geometry = c4_pbr_smith(nv, alphaSquared) * c4_pbr_smith(nl, alphaSquared);
    vec3 specular = fresnel * (distribution * geometry / max(4.0 * nv * nl, 1.0e-6));
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic)
                 * c4_quality_srgb_to_linear(sourceAlbedoSrgb) / c4_pbr_pi;
    return (diffuse + specular) * lightRadianceLinear * nl;
}

vec3 c4_quality_direct_metallic(vec3 albedo, float metallic, float roughness,
                               float mask, vec3 normal, vec3 view, vec3 light,
                               vec3 radiance) {
    vec3 f0 = mix(vec3(0.04), c4_quality_srgb_to_linear(albedo), metallic);
    return c4_pbr_direct(albedo, f0, roughness, metallic, mask, normal, view, light, radiance);
}

// The recovered source SUN is encoded in the complete technique result: fixed
// MODULATE2X/tint/clamps and Normal FX skill/lightmap/cube factors all participate.
// Convert its *difference* to linear energy so the source residual remains
// intact; separately decoding the sRGB summands would lose their cross term.
// A unit-Lambert reference redistributes this energy through the native GGX
// shape, rather than inventing a weaker radiance from an unrelated SUN colour.
vec3 c4_quality_source_sun_metallic(vec3 sourceDirectSrgb,
        vec3 sourceResidualSrgb, float metallic, float roughness, float mask,
        vec3 normal, vec3 view, vec3 light) {
    float nl = max(dot(c4_pbr_unit(normal), c4_pbr_unit(light)), 0.0);
    if (nl <= 0.0) return vec3(0.0);
    vec3 energy = c4_quality_srgb_to_linear(max(sourceResidualSrgb + sourceDirectSrgb, vec3(0.0)))
                - c4_quality_srgb_to_linear(max(sourceResidualSrgb, vec3(0.0)));
    // Source energy already contains albedo/tint. Only redistribute its shape;
    // tinting metallic F0 again would apply dark/coloured source albedo twice.
    vec3 f0 = mix(vec3(0.04), vec3(1.0), metallic);
    return energy * c4_pbr_direct(vec3(1.0), f0, roughness, metallic, mask,
        normal, view, light, vec3(c4_pbr_pi / nl));
}

// Inputs are cached authored-cube cosine convolution (irradiance/pi) and
// GGX roughness-prefiltered linear radiance, never raw-cube renamed as IBL.
vec3 c4_quality_environment_metallic(vec3 albedo, float metallic, float roughness,
        float reflectivity, vec3 normal, vec3 view, vec3 irradiance,
        vec3 prefiltered) {
    vec3 base = c4_quality_srgb_to_linear(albedo);
    float nv = max(dot(c4_pbr_unit(normal), c4_pbr_unit(view)), 0.0);
    vec3 f0 = mix(vec3(0.04), base, metallic);
    vec3 fresnel = c4_pbr_schlick(f0, nv);
    // Analytic split-sum BRDF approximation; the environment itself is
    // actually convolved. This is native material approximation, not FX parity.
    vec4 r = roughness * vec4(-1.0, -0.0275, -0.572, 0.022)
                        + vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    vec2 brdf = vec2(-1.04, 1.04) * a004 + r.zw;
    return reflectivity * ((vec3(1.0) - fresnel) * (1.0 - metallic) * base * irradiance
                         + prefiltered * (f0 * brdf.x + brdf.y));
}

#endif
