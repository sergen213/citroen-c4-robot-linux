#ifndef C4_QUALITY_POST_GLSL
#define C4_QUALITY_POST_GLSL

#ifndef C4_QUALITY_TEXTURE
#define C4_QUALITY_TEXTURE texture2D
#endif

// Enhanced-only helpers, GLSL 1.20/4.50. Owners supply textures, matrices and
// successful passes; none of these functions replaces Original gamma/bloom.
// UVs are normalized and texelSize is (1/width,1/height). Sampling clamps to
// texel centers. RGB inputs are nonnegative; FXAA takes SDR display-encoded RGB,
// bloom takes scene-linear HDR. Alpha/compositing remain the caller's policy.
bool c4_post_finite(float x) {
    return x >= -1.0e10 && x <= 1.0e10;
}

float c4_post_channel(float x) {
    if (!(x >= 0.0)) return 0.0;
    return min(x, 1.0e10);
}

vec3 c4_post_rgb(vec3 x) {
    return vec3(c4_post_channel(x.r), c4_post_channel(x.g), c4_post_channel(x.b));
}

bool c4_post_valid_uv(vec2 uv) {
    return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

bool c4_post_valid_texel(vec2 texelSize) {
    return texelSize.x > 0.0 && texelSize.x <= 1.0
        && texelSize.y > 0.0 && texelSize.y <= 1.0;
}

vec2 c4_post_uv(vec2 uv, vec2 texelSize) {
    return clamp(uv, texelSize * 0.5, vec2(1.0) - texelSize * 0.5);
}

vec3 c4_post_sample(sampler2D image, vec2 uv, vec2 texelSize) {
    return c4_post_rgb(C4_QUALITY_TEXTURE(image, c4_post_uv(uv, texelSize)).rgb);
}

float c4_post_luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

float c4_post_fxaa_step(int i) {
    if (i == 0) return 1.5;
    if (i < 5) return 2.0;
    if (i < 7) return 4.0;
    return 8.0;
}

// FXAA: neighborhood contrast rejection, edge orientation, two-sided endpoint
// search, polarity rejection and subpixel coverage blending. Returns SDR RGB;
// run after tone mapping, and never filter depth/normals with this function.
vec3 c4_quality_fxaa(sampler2D image, vec2 uv, vec2 texelSize) {
    if (!c4_post_valid_uv(uv) || !c4_post_valid_texel(texelSize)) return vec3(0.0);
    vec3 center = c4_post_sample(image, uv, texelSize);
    float m = c4_post_luma(center);
    float n = c4_post_luma(c4_post_sample(image, uv - vec2(0.0, texelSize.y), texelSize));
    float s = c4_post_luma(c4_post_sample(image, uv + vec2(0.0, texelSize.y), texelSize));
    float w = c4_post_luma(c4_post_sample(image, uv - vec2(texelSize.x, 0.0), texelSize));
    float e = c4_post_luma(c4_post_sample(image, uv + vec2(texelSize.x, 0.0), texelSize));
    float low = min(m, min(min(n, s), min(w, e)));
    float high = max(m, max(max(n, s), max(w, e)));
    float contrast = high - low;
    if (contrast < max(0.0625, high * 0.125)) return center;

    float nw = c4_post_luma(c4_post_sample(image, uv - texelSize, texelSize));
    float ne = c4_post_luma(c4_post_sample(image, uv + vec2(texelSize.x, -texelSize.y), texelSize));
    float sw = c4_post_luma(c4_post_sample(image, uv + vec2(-texelSize.x, texelSize.y), texelSize));
    float se = c4_post_luma(c4_post_sample(image, uv + texelSize, texelSize));
    float horizontal = abs(nw - 2.0 * w + sw) + 2.0 * abs(n - 2.0 * m + s)
        + abs(ne - 2.0 * e + se);
    float vertical = abs(nw - 2.0 * n + ne) + 2.0 * abs(w - 2.0 * m + e)
        + abs(sw - 2.0 * s + se);
    bool horizontalEdge = horizontal >= vertical;
    float first = horizontalEdge ? n : w;
    float second = horizontalEdge ? s : e;
    float firstGradient = abs(first - m);
    float secondGradient = abs(second - m);
    bool firstSteeper = firstGradient >= secondGradient;
    float gradient = max(firstGradient, secondGradient);
    float edgeLuma = 0.5 * (m + (firstSteeper ? first : second));
    vec2 normalStep = horizontalEdge ? vec2(0.0, texelSize.y) : vec2(texelSize.x, 0.0);
    if (firstSteeper) normalStep = -normalStep;
    vec2 tangentStep = horizontalEdge ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);
    vec2 edgeUv = uv + normalStep * 0.5;
    vec2 negativeUv = edgeUv - tangentStep;
    vec2 positiveUv = edgeUv + tangentStep;
    float negativeDelta = c4_post_luma(c4_post_sample(image, negativeUv, texelSize)) - edgeLuma;
    float positiveDelta = c4_post_luma(c4_post_sample(image, positiveUv, texelSize)) - edgeLuma;
    float endThreshold = gradient * 0.25;
    bool negativeEnd = abs(negativeDelta) >= endThreshold;
    bool positiveEnd = abs(positiveDelta) >= endThreshold;
    for (int i = 0; i < 12; ++i) {
        if (negativeEnd && positiveEnd) break;
        float distanceStep = c4_post_fxaa_step(i);
        if (!negativeEnd) {
            negativeUv -= tangentStep * distanceStep;
            negativeDelta = c4_post_luma(c4_post_sample(image, negativeUv, texelSize)) - edgeLuma;
            negativeEnd = abs(negativeDelta) >= endThreshold || !c4_post_valid_uv(negativeUv);
        }
        if (!positiveEnd) {
            positiveUv += tangentStep * distanceStep;
            positiveDelta = c4_post_luma(c4_post_sample(image, positiveUv, texelSize)) - edgeLuma;
            positiveEnd = abs(positiveDelta) >= endThreshold || !c4_post_valid_uv(positiveUv);
        }
    }
    float negativeDistance = horizontalEdge ? uv.x - negativeUv.x : uv.y - negativeUv.y;
    float positiveDistance = horizontalEdge ? positiveUv.x - uv.x : positiveUv.y - uv.y;
    bool negativeCloser = negativeDistance < positiveDistance;
    float nearestDelta = negativeCloser ? negativeDelta : positiveDelta;
    float span = negativeDistance + positiveDistance;
    float edgeOffset = 0.0;
    if (span > 0.0 && ((nearestDelta < 0.0) != (m < edgeLuma))) {
        edgeOffset = 0.5 - min(negativeDistance, positiveDistance) / span;
    }
    float average = (2.0 * (n + s + w + e) + nw + ne + sw + se) / 12.0;
    float subpixel = clamp(abs(average - m) / max(contrast, 1.0e-6), 0.0, 1.0);
    subpixel = subpixel * subpixel * (3.0 - 2.0 * subpixel);
    subpixel = subpixel * subpixel * 0.75;
    return c4_post_sample(image, uv + normalStep * max(edgeOffset, subpixel), texelSize);
}

float c4_post_srgb(float linearValue) {
    if (linearValue <= 0.0031308) return 12.92 * linearValue;
    return 1.055 * pow(linearValue, 1.0 / 2.4) - 0.055;
}

// exposureMultiplier is 2^EV, not EV. aces >= 0.5 selects the standard fitted
// ACES filmic curve; otherwise Reinhard. Input is scene-linear internal HDR,
// output is [0,1] sRGB SDR (IEC 61966-2-1), NOT the Original gamma equation.
vec3 c4_quality_tone_map(vec3 linearHdr, float exposureMultiplier, float aces) {
    if (!(exposureMultiplier >= 0.0) || !c4_post_finite(exposureMultiplier)) return vec3(0.0);
    vec3 exposed = c4_post_rgb(c4_post_rgb(linearHdr) * exposureMultiplier);
    vec3 mapped;
    if (aces >= 0.5) {
        mapped = (exposed * (2.51 * exposed + vec3(0.03)))
            / (exposed * (2.43 * exposed + vec3(0.59)) + vec3(0.14));
    } else {
        mapped = exposed / (vec3(1.0) + exposed);
    }
    mapped = clamp(mapped, 0.0, 1.0);
    return vec3(c4_post_srgb(mapped.r), c4_post_srgb(mapped.g), c4_post_srgb(mapped.b));
}

// Luminance bright-pass preserves chromaticity. threshold is scene-linear HDR
// luminance, before exposure/tone mapping. Caller owns bloom strength/composite.
vec3 c4_quality_bloom_bright(vec3 linearHdr, float threshold) {
    vec3 color = c4_post_rgb(linearHdr);
    if (!(threshold >= 0.0) || !c4_post_finite(threshold)) return vec3(0.0);
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return color * (max(luminance - threshold, 0.0) / max(luminance, 1.0e-6));
}

float c4_post_gaussian_weight(int distanceInTexels) {
    if (distanceInTexels == 0) return 0.2270270270;
    if (distanceInTexels == 1) return 0.1945945946;
    if (distanceInTexels == 2) return 0.1216216216;
    if (distanceInTexels == 3) return 0.0540540541;
    return 0.0162162162;
}

// Separable nine-tap Gaussian: call into distinct attachments with axis=(1,0)
// then (0,1). HDR RGB remains scene-linear; weights normalize even at borders.
vec3 c4_quality_gaussian(sampler2D image, vec2 uv, vec2 texelSize, vec2 axis) {
    if (!c4_post_valid_uv(uv) || !c4_post_valid_texel(texelSize)) return vec3(0.0);
    if (!c4_post_finite(axis.x) || !c4_post_finite(axis.y)) return vec3(0.0);
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int i = -4; i <= 4; ++i) {
        int distanceInTexels = i < 0 ? -i : i;
        float weight = c4_post_gaussian_weight(distanceInTexels);
        sum += c4_post_sample(image, uv + axis * texelSize * float(i), texelSize) * weight;
        weightSum += weight;
    }
    return sum / max(weightSum, 1.0e-6);
}

// Depth/normal contract for BOTH AO functions:
// depthImage.r is hardware depth [0,1], sampled without linear filtering;
// backgroundDepth is the actual clear depth (usually 1, or 0 reversed-Z).
// clipScaleBias maps sampled depth to clip Z: GL=(2,-1), Vulkan=(1,0).
// Matrices map normalized UV via clipXY=UV*2-1; caller accounts for framebuffer
// Y orientation in the supplied projection/inverseProjection, not in normals.
// View space is right-handed, visible positions have negative Z. normalImage
// RGB stores a normalized VIEW-space normal encoded as N*0.5+0.5, sampled
// nearest; an invalid/zero normal is unoccluded. Owners must write this exact
// convention, not world normals, packed octahedra or material normal maps.
// AO is visibility: unoccluded/background/invalid input returns 1, occluded 0.
bool c4_post_view_position(sampler2D depthImage, vec2 uv, mat4 inverseProjection,
                          vec2 clipScaleBias, float backgroundDepth, out vec3 position) {
    position = vec3(0.0);
    if (!c4_post_valid_uv(uv) || !c4_post_finite(backgroundDepth)
        || !c4_post_finite(clipScaleBias.x) || !c4_post_finite(clipScaleBias.y)) return false;
    float depth = C4_QUALITY_TEXTURE(depthImage, uv).r;
    if (!(depth >= 0.0 && depth <= 1.0) || abs(depth - backgroundDepth) <= 1.0e-6) return false;
    vec4 homogeneous = inverseProjection * vec4(uv * 2.0 - 1.0, depth * clipScaleBias.x + clipScaleBias.y, 1.0);
    if (!c4_post_finite(homogeneous.w) || abs(homogeneous.w) <= 1.0e-8) return false;
    position = homogeneous.xyz / homogeneous.w;
    return c4_post_finite(position.x) && c4_post_finite(position.y)
        && c4_post_finite(position.z) && position.z < -1.0e-6;
}

bool c4_post_view_normal(sampler2D normalImage, vec2 uv, out vec3 normal) {
    vec3 encoded = C4_QUALITY_TEXTURE(normalImage, uv).rgb;
    normal = vec3(0.0);
    if (!(encoded.x >= 0.0 && encoded.x <= 1.0 && encoded.y >= 0.0
        && encoded.y <= 1.0 && encoded.z >= 0.0 && encoded.z <= 1.0)) return false;
    vec3 decoded = encoded * 2.0 - 1.0;
    float lengthSquared = dot(decoded, decoded);
    if (!(lengthSquared > 1.0e-6)) return false;
    normal = decoded * inversesqrt(lengthSquared);
    return true;
}

// AO describes the visible side of the depth surface, not the source material's
// one-/two-sided lighting normal. Inward/backface mesh normals must not send
// the sampling hemisphere into the surface and occlude the surface itself.
vec3 c4_post_visible_normal(vec3 unitNormal, vec3 viewPosition) {
    return dot(unitNormal, -viewPosition) < 0.0 ? -unitNormal : unitNormal;
}

// Arithmetic golden-angle hemisphere kernel, no texture/arrays/bitwise ops.
// Count is exactly 16 or 32. Directions are deterministic; quadratic radial
// scaling places more samples near the surface, as in standard kernel SSAO.
vec3 c4_post_hemisphere(int index, int count) {
    float fraction = (float(index) + 0.5) / float(count);
    float z = fraction;
    float radial = sqrt(max(1.0 - z * z, 0.0));
    float angle = float(index) * 2.3999632297;
    float scale = mix(0.1, 1.0, fraction * fraction);
    return vec3(cos(angle) * radial, sin(angle) * radial, z) * scale;
}

// radius and bias are nonnegative VIEW-space distances; sampleCount selects
// 16 (<=16) or 32 (>16). Reconstructs from depth and samples a normal-oriented
// hemisphere, projects each sample, compares negative view Z, and attenuates
// occluders outside the radius. Missing/offscreen/background samples are open.
float c4_quality_ssao(sampler2D depthImage, sampler2D normalImage, vec2 uv,
                      vec2 texelSize, mat4 projection, mat4 inverseProjection,
                      vec2 clipScaleBias, float backgroundDepth, float radius,
                      float bias, int sampleCount) {
    if (!c4_post_valid_uv(uv) || !c4_post_valid_texel(texelSize)
        || !(radius > 0.0) || !c4_post_finite(radius)
        || !(bias >= 0.0) || !c4_post_finite(bias) || sampleCount <= 0) return 1.0;
    uv = c4_post_uv(uv, texelSize);
    vec3 origin;
    vec3 normal;
    if (!c4_post_view_position(depthImage, uv, inverseProjection, clipScaleBias, backgroundDepth, origin)
        || !c4_post_view_normal(normalImage, uv, normal)) return 1.0;
    normal = c4_post_visible_normal(normal, origin);
    vec3 reference = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(reference, normal));
    vec3 bitangent = cross(normal, tangent);
    int count = sampleCount > 16 ? 32 : 16;
    float occlusion = 0.0;
    for (int i = 0; i < 32; ++i) {
        if (i >= count) break;
        vec3 kernel = c4_post_hemisphere(i, count);
        vec3 samplePosition = origin + radius * (tangent * kernel.x + bitangent * kernel.y + normal * kernel.z);
        if (!(samplePosition.z < -1.0e-6)) continue;
        vec4 clip = projection * vec4(samplePosition, 1.0);
        if (!(clip.w > 1.0e-8) || !c4_post_finite(clip.w)) continue;
        vec3 ndc = clip.xyz / clip.w;
        if (!c4_post_finite(ndc.x) || !c4_post_finite(ndc.y) || !c4_post_finite(ndc.z)) continue;
        float clipLow = min(clipScaleBias.y, clipScaleBias.x + clipScaleBias.y);
        float clipHigh = max(clipScaleBias.y, clipScaleBias.x + clipScaleBias.y);
        if (ndc.z < clipLow || ndc.z > clipHigh) continue;
        vec2 sampleUv = ndc.xy * 0.5 + 0.5;
        if (!c4_post_valid_uv(sampleUv)) continue;
        sampleUv = c4_post_uv(sampleUv, texelSize);
        vec3 surface;
        if (!c4_post_view_position(depthImage, sampleUv, inverseProjection, clipScaleBias, backgroundDepth, surface)) continue;
        float range = smoothstep(0.0, 1.0, radius / max(abs(origin.z - surface.z), 1.0e-6));
        if (surface.z >= samplePosition.z + bias) occlusion += range;
    }
    return clamp(1.0 - occlusion / float(count), 0.0, 1.0);
}

// Separable nine-tap joint bilateral AO filter. axis=(1,0), then (0,1), using
// distinct attachments. depthSigma > 0 is VIEW-space depth standard deviation;
// normalCosThreshold is [0,1), rejecting normals at/below that dot product.
// Background never bleeds into geometry. Spatial/depth/normal weights ALWAYS
// normalize; invalid samples are skipped and an empty neighborhood returns 1.
float c4_quality_ao_bilateral(sampler2D aoImage, sampler2D depthImage,
                              sampler2D normalImage, vec2 uv, vec2 texelSize,
                              vec2 axis, mat4 inverseProjection, vec2 clipScaleBias,
                              float backgroundDepth, float depthSigma,
                              float normalCosThreshold) {
    if (!c4_post_valid_uv(uv) || !c4_post_valid_texel(texelSize)
        || !c4_post_finite(axis.x) || !c4_post_finite(axis.y)
        || !(depthSigma > 0.0) || !c4_post_finite(depthSigma)
        || !(normalCosThreshold >= 0.0 && normalCosThreshold < 1.0)) return 1.0;
    uv = c4_post_uv(uv, texelSize);
    vec3 centerPosition;
    vec3 centerNormal;
    if (!c4_post_view_position(depthImage, uv, inverseProjection, clipScaleBias, backgroundDepth, centerPosition)
        || !c4_post_view_normal(normalImage, uv, centerNormal)) return 1.0;
    float sum = 0.0;
    float weightSum = 0.0;
    for (int i = -4; i <= 4; ++i) {
        vec2 sampleUv = uv + axis * texelSize * float(i);
        if (!c4_post_valid_uv(sampleUv)) continue;
        sampleUv = c4_post_uv(sampleUv, texelSize);
        vec3 samplePosition;
        vec3 sampleNormal;
        if (!c4_post_view_position(depthImage, sampleUv, inverseProjection, clipScaleBias, backgroundDepth, samplePosition)
            || !c4_post_view_normal(normalImage, sampleUv, sampleNormal)) continue;
        float similarity = dot(centerNormal, sampleNormal);
        if (similarity <= normalCosThreshold) continue;
        float ao = C4_QUALITY_TEXTURE(aoImage, sampleUv).r;
        if (!(ao >= 0.0 && ao <= 1.0)) continue;
        float depthDistance = abs(samplePosition.z - centerPosition.z) / depthSigma;
        // Avoid overflow/underflow work for effectively zero Gaussian weights.
        if (!(depthDistance < 8.0)) continue;
        int distanceInTexels = i < 0 ? -i : i;
        float weight = c4_post_gaussian_weight(distanceInTexels)
            * exp(-0.5 * depthDistance * depthDistance)
            * smoothstep(normalCosThreshold, 1.0, similarity);
        sum += ao * weight;
        weightSum += weight;
    }
    if (!(weightSum > 1.0e-8)) return 1.0;
    return clamp(sum / weightSum, 0.0, 1.0);
}

#endif
