#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../material_bloom.glsl"
#define C4_QUALITY_TEXTURE texture
#include "../../quality/quality_post.glsl"

layout(constant_id = 2) const uint composeMode = 0u;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1, std140) uniform Globals {
    mat4 viewProjection;
    vec4 viewPosition;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 ambient;
    vec4 fogColor;
    vec4 fogParams;
    vec4 bloomParams;
    vec4 bloomExtra;
    vec4 viewDirection;
    mat4 viewMatrix;
    vec4 renderOptions;
} globals;
layout(set = 0, binding = 2) uniform sampler2D blurColor;
layout(set = 0, binding = 3) uniform sampler2D gammaLUT;
// Private post layout preserves common source bindings. b4 is the resolved
// hardware-depth image, never an average of the world-position MRT.
layout(set = 0, binding = 4) uniform sampler2D qualityDepth;
layout(set = 0, binding = 5) uniform sampler2D qualityNormal;
layout(set = 0, binding = 6) uniform sampler2D qualityAO;
layout(set = 0, binding = 7) uniform sampler2D qualityBloom;
layout(set = 0, binding = 11, std140) uniform Quality {
    vec4 effects;
    vec4 post;
    vec4 sampling;
} quality;
layout(set = 0, binding = 14, std140) uniform PostMatrices {
    mat4 projection;
    mat4 inverseProjection;
} camera;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main() {
    if (composeMode == 9u) {
        vec4 world = texture(qualityNormal, uv);
        vec3 normal = mat3(globals.viewMatrix) * world.xyz;
        float magnitude = dot(normal, normal);
        color = vec4(0.5, 0.5, 0.5, 0.0);
        vec3 viewPosition;
        if (world.a > 0.0 && magnitude > 1.0e-8 &&
            c4_post_view_position(qualityDepth, uv, camera.inverseProjection,
                vec2(1.0, 0.0), 1.0, viewPosition)) {
            normal = c4_post_visible_normal(normal * inversesqrt(magnitude), viewPosition);
            color = vec4(normal * 0.5 + 0.5, 1.0);
        }
        return;
    }
    if (composeMode == 10u) {
        float visibility = c4_quality_ssao(qualityDepth, qualityNormal, uv,
            quality.sampling.xy, camera.projection, camera.inverseProjection,
            vec2(1.0, 0.0), 1.0, quality.post.w, quality.sampling.z,
            int(quality.effects.y));
        color = vec4(visibility, visibility, visibility, 1.0);
        return;
    }
    if (composeMode == 11u || composeMode == 12u) {
        vec2 axis = composeMode == 11u ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
        float visibility = c4_quality_ao_bilateral(qualityAO, qualityDepth,
            qualityNormal, uv, quality.sampling.xy, axis,
            camera.inverseProjection, vec2(1.0, 0.0), 1.0,
            max(quality.post.w * 0.25, 0.001), 0.8);
        color = vec4(visibility, visibility, visibility, 1.0);
        return;
    }
    if (composeMode == 13u) {
        color = vec4(c4_quality_bloom_bright(texture(sceneColor, uv).rgb, 1.0), 1.0);
        return;
    }
    if (composeMode == 14u || composeMode == 15u) {
        vec2 axis = composeMode == 14u ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
        color = vec4(c4_quality_gaussian(sceneColor, uv,
            1.0 / vec2(textureSize(sceneColor, 0)), axis), 1.0);
        return;
    }
    if (composeMode == 16u) {
        color = texture(sceneColor, uv);
        float visibility = quality.effects.y > 0.0 ? texture(qualityAO, uv).r : 1.0;
        vec3 scene = color.rgb * visibility;
        if (quality.post.y > 0.0) scene += texture(qualityBloom, uv).rgb * quality.post.y;
        color.rgb = quality.effects.w > 0.0
            ? c4_quality_tone_map(scene, quality.post.x, quality.post.z)
            : clamp(scene * quality.post.x, 0.0, 1.0);
        return;
    }
    if (composeMode == 17u) {
        color = vec4(c4_quality_fxaa(sceneColor, uv, quality.sampling.xy),
                     texture(sceneColor, uv).a);
        return;
    }
    if (composeMode == 0u) {
        color = texture(sceneColor, uv);
    } else if (composeMode == 1u) {
        color = c4BloomDown(
            texture(sceneColor, c4BloomDownCoordinate(uv, 0)),
            texture(sceneColor, c4BloomDownCoordinate(uv, 1)),
            texture(sceneColor, c4BloomDownCoordinate(uv, 2)),
            texture(sceneColor, c4BloomDownCoordinate(uv, 3)),
            globals.bloomParams.x);
    } else if (composeMode == 2u) {
        color = vec4(0.0);
        for (int tap = 0; tap <= 20; ++tap) {
            color += c4BloomTap(texture(sceneColor,
                c4BloomHCoordinate(uv, tap, globals.bloomParams.y)));
        }
    } else if (composeMode == 3u) {
        color = vec4(0.0);
        for (int tap = 0; tap <= 20; ++tap) {
            color += c4BloomTap(texture(sceneColor,
                c4BloomVCoordinate(uv, tap, globals.bloomParams.y)));
        }
    } else if (composeMode == 5u) {
        color = texture(sceneColor, uv);
        if (globals.renderOptions.x != 0.0) {
            ivec3 indices = ivec3(round(clamp(color.rgb, 0.0, 1.0) * 255.0));
            color.r = texelFetch(gammaLUT, ivec2(indices.r, 0), 0).r;
            color.g = texelFetch(gammaLUT, ivec2(indices.g, 0), 0).g;
            color.b = texelFetch(gammaLUT, ivec2(indices.b, 0), 0).b;
        }
    } else {
        color = c4BloomComposite(texture(sceneColor, uv),
            texture(blurColor, uv), globals.bloomParams.z,
            globals.bloomParams.w, globals.bloomExtra.x);
    }
}
