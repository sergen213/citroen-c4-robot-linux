#ifndef C4_QUALITY_SHADOW_GLSL
#define C4_QUALITY_SHADOW_GLSL

#ifndef C4_QUALITY_TEXTURE
#define C4_QUALITY_TEXTURE texture2D
#endif

// Explicit branches work in GLSL 120: no dynamically indexed matrix arrays.
// Depth is positive camera-view distance, not device depth.
int c4_quality_shadow_cascade(float positiveViewDepth, vec4 splitDepths, int count) {
    if (positiveViewDepth < 0.0 || count <= 0) return -1;
    if (positiveViewDepth <= splitDepths.x) return 0;
    if (count > 1 && positiveViewDepth <= splitDepths.y) return 1;
    if (count > 2 && positiveViewDepth <= splitDepths.z) return 2;
    if (count > 3 && positiveViewDepth <= splitDepths.w) return 3;
    return -1;
}

// Receiver-only normalized bias: half a constant texel plus at most one slope
// texel from the cascade policy, without a second raster-depth offset.
float c4_quality_shadow_reference(float depth, float normalDotLight,
                                  float constantBias, float slopeBias) {
    float cosine = clamp(normalDotLight, 0.0, 1.0);
    float slope = min(sqrt(max(1.0 - cosine * cosine, 0.0)) / max(cosine, 0.1), 1.0);
    return depth - max(constantBias, 0.0) - max(slopeBias, 0.0) * slope;
}

float c4_quality_shadow_bilinear(vec4 comparisons, vec2 fraction) {
    return mix(mix(comparisons.x, comparisons.y, fraction.x),
               mix(comparisons.z, comparisons.w, fraction.x), fraction.y);
}

// atlasTile=(offset.xy, scale.xy); signed scales support Vulkan's Y flip.
// atlasTexel=1/atlasSize. Depth scale/bias is (.5,.5) for [-1,1] light clip
// or (1,0) for [0,1]. Both bias inputs are normalized shadow-depth units.
float c4_quality_shadow_pcf(sampler2D atlas, vec4 lightClip, vec4 atlasTile,
                            vec2 atlasTexel, vec2 clipDepthScaleBias,
                            float normalDotLight, float constantBias, float slopeBias) {
    if (lightClip.w <= 0.0 || atlasTexel.x <= 0.0 || atlasTexel.y <= 0.0)
        return 1.0;
    vec3 projected = lightClip.xyz / lightClip.w;
    vec2 localUv = projected.xy * 0.5 + 0.5;
    float depth = projected.z * clipDepthScaleBias.x + clipDepthScaleBias.y;
    if (localUv.x < 0.0 || localUv.x > 1.0 || localUv.y < 0.0 || localUv.y > 1.0
        || depth < 0.0 || depth > 1.0) return 1.0;
    vec2 tileMin = min(atlasTile.xy, atlasTile.xy + atlasTile.zw) + atlasTexel * 0.5;
    vec2 tileMax = max(atlasTile.xy, atlasTile.xy + atlasTile.zw) - atlasTexel * 0.5;
    if (tileMin.x > tileMax.x || tileMin.y > tileMax.y) return 1.0;
    float reference = c4_quality_shadow_reference(depth, normalDotLight, constantBias, slopeBias);
    vec2 uv = atlasTile.xy + localUv * atlasTile.zw;
    // Interpolate comparisons, not depths. Nearest depth textures retain the
    // four independent comparisons; texel centres make subtexel coverage smooth.
    vec2 position = uv / atlasTexel - 0.5;
    vec2 lower = (floor(position) + 0.5) * atlasTexel;
    vec2 upper = clamp(lower + atlasTexel, tileMin, tileMax);
    lower = clamp(lower, tileMin, tileMax);
    vec4 comparisons = vec4(
        step(reference, C4_QUALITY_TEXTURE(atlas, lower).r),
        step(reference, C4_QUALITY_TEXTURE(atlas, vec2(upper.x, lower.y)).r),
        step(reference, C4_QUALITY_TEXTURE(atlas, vec2(lower.x, upper.y)).r),
        step(reference, C4_QUALITY_TEXTURE(atlas, upper).r));
    return c4_quality_shadow_bilinear(comparisons, fract(position));
}

// Vulkan depth-array owner opts in under GLSL 450. Input matrices remain the
// shared GL-clip matrices; the depth draw flips Y and maps Z to [0,1].
#ifdef C4_QUALITY_SHADOW_ARRAY
float c4_quality_shadow_array_pcf(sampler2DArray shadowMaps, vec4 lightClip,
                                  float layer, vec2 texel, float normalDotLight,
                                  float constantBias, float slopeBias) {
    if (lightClip.w <= 0.0 || texel.x <= 0.0 || texel.y <= 0.0
        || texel.x > 1.0 || texel.y > 1.0 || layer < 0.0) return 1.0;
    vec3 projected = lightClip.xyz / lightClip.w;
    vec2 uv = vec2(projected.x, -projected.y) * 0.5 + 0.5;
    float depth = projected.z * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0
        || depth < 0.0 || depth > 1.0) return 1.0;
    float reference = c4_quality_shadow_reference(depth, normalDotLight, constantBias, slopeBias);
    vec2 position = uv / texel - 0.5;
    vec2 lower = (floor(position) + 0.5) * texel;
    vec2 upper = clamp(lower + texel, texel * 0.5, vec2(1.0) - texel * 0.5);
    lower = clamp(lower, texel * 0.5, vec2(1.0) - texel * 0.5);
    vec4 comparisons = vec4(
        step(reference, texture(shadowMaps, vec3(lower, layer)).r),
        step(reference, texture(shadowMaps, vec3(upper.x, lower.y, layer)).r),
        step(reference, texture(shadowMaps, vec3(lower.x, upper.y, layer)).r),
        step(reference, texture(shadowMaps, vec3(upper, layer)).r));
    return c4_quality_shadow_bilinear(comparisons, fract(position));
}
#endif

#endif
