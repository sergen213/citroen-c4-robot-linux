#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../material_normal.glsl"
#include "../../material_fixed.glsl"
#include "../../source_lighting.glsl"
#define C4_QUALITY_TEXTURE texture
#define C4_QUALITY_SHADOW_ARRAY
#include "../../quality/quality_pbr.glsl"
#include "../../quality/quality_post.glsl"
#include "../../quality/quality_shadow.glsl"

layout(constant_id = 1) const bool overlayPass = false;
layout(constant_id = 3) const bool effectPass = false;
layout(constant_id = 4) const bool shadowPass = false;
layout(constant_id = 5) const bool hudMesh = false;
layout(constant_id = 6) const bool skyPass = false;

layout(set = 0, binding = 0) uniform sampler2D entSkin1;
layout(set = 0, binding = 2) uniform sampler2D entSkin2;
layout(set = 0, binding = 3) uniform sampler2D entSkin3;
layout(set = 0, binding = 4) uniform sampler2D entSkin4;
layout(set = 0, binding = 5) uniform sampler2D mtlSkin1;
layout(set = 0, binding = 6) uniform sampler2D mtlSkin2;
layout(set = 0, binding = 7) uniform sampler2D mtlSkin3;
layout(set = 0, binding = 8) uniform sampler2D mtlSkin4;
layout(set = 0, binding = 9) uniform samplerCube mtlSkin1Cube;
layout(set = 0, binding = 12) uniform sampler2DArray shadowMaps;
layout(set = 0, binding = 16) uniform samplerCube modernIrradiance;
layout(set = 0, binding = 17) uniform samplerCube modernPrefiltered;

layout(set = 0, binding = 1, std140) uniform FrameUniforms {
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
} frame;

layout(set = 0, binding = 10, std140) uniform MaterialUniforms {
    uvec4 config;
    vec4 diffuse;
    vec4 ambient;
    vec4 specular;
    vec4 emissive;
    vec4 skills[5];
    vec4 params;
    mat4 textureTransform;
    vec4 bumpMatrix;
    vec4 textureFactor;
    vec4 extra;
    vec4 modern;
    vec4 environment;
} material;

layout(set = 0, binding = 11, std140) uniform QualityUniforms {
    vec4 effects;
    vec4 post;
    vec4 sampling;
} quality;

layout(set = 0, binding = 13, std140) uniform ShadowUniforms {
    mat4 matrices[4];
    vec4 splits;
    vec4 viewForward;
    vec4 viewPosition;
    vec4 params;
    vec4 sunDirection;
    vec4 constantBias;
    vec4 slopeBias;
} shadow;

layout(set = 0, binding = 15, std140) uniform SourceLightingUniforms {
    C4SourceLighting state;
} sourceLighting;

layout(push_constant) uniform DrawConstants {
    layout(offset = 0) mat4 model;
    layout(offset = 64) vec4 diffuse;
    layout(offset = 80) vec4 uvRect;
    layout(offset = 96) uvec4 config;
    layout(offset = 112) vec4 params;
} draw;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 worldPosition;
layout(location = 2) in vec3 worldNormal;
layout(location = 3) in vec2 uv1;
layout(location = 4) in vec3 tangentView;
layout(location = 5) in vec3 tangentSun;
layout(location = 6) in vec3 cubeDirection;
layout(location = 7) in vec4 vertexDiffuse;
layout(location = 8) in vec3 shadingTangent;
layout(location = 9) in vec3 shadingBinormal;
layout(location = 10) in vec3 shadingNormal;
layout(location = 11) in vec3 vertexSpecular;
layout(location = 12) in float vertexFog;
layout(location = 13) in vec3 sunlessDiffuse;
layout(location = 14) in vec3 sunlessSpecular;

layout(location = 0) out vec4 baseColor;
layout(location = 1) out vec4 positionOutput;
layout(location = 2) out vec4 normalOutput;
layout(location = 3) out vec4 materialOutput;
layout(location = 4) out vec4 sunOutput;

const uint FixedFunction = 0;
const uint NormalOptim = 1;
const uint NormalTileOptim = 2;
const uint NormalTileSpec = 3;
const uint NormalMix = 4;
const uint NormalTOptShadow = 5;
const uint NormalTSpecShadow = 6;
const uint CubeNormalOptim = 7;
const uint CubeNormalCar = 8;
const uint CubeNormalMix = 9;
const uint EnvCubeMix = 10;
const uint EnvCubeAlpha = 11;
const uint ShadowMap = 12;
const uint Terrain4A = 13;
const uint Terrain5C = 18;
const uint Water = 19;

vec4 terrainSample(uint passIndex, vec2 coordinates) {
    switch (passIndex) {
        case 1: return texture(mtlSkin1, coordinates);
        case 2: return texture(mtlSkin2, coordinates);
        case 3: return texture(mtlSkin3, coordinates);
        case 4: return texture(mtlSkin4, coordinates);
        default: return texture(entSkin1, coordinates);
    }
}

vec4 sourceMaterial(vec4 colorSample, out vec4 normalSample, out vec3 albedo,
                    bool collectSunless, out vec3 sunlessRgb) {
    sunlessRgb = vec3(0.0);
    normalSample = vec4(0.0);
    albedo = colorSample.rgb * sourceLighting.state.diffuse.rgb;
    uint technique = material.config.x;
    bool fixedFunction = technique == FixedFunction ||
                         (technique >= EnvCubeMix && technique <= Water);
    if (material.config.z == 0 ||
        (fixedFunction && sourceLighting.state.status.x == 0)) {
        // The CPU diagnoses missing source inputs; this is not claimed FX parity.
        return colorSample * draw.diffuse;
    }
    if (technique == FixedFunction) {
        if (collectSunless) {
            sunlessRgb = c4FixedModel(colorSample.rgb, sunlessDiffuse,
                                      sourceLighting.state.materialParams.y == 2.0);
        }
        return vec4(c4FixedModel(colorSample.rgb, vertexDiffuse.rgb,
                                sourceLighting.state.materialParams.y == 2.0),
                    colorSample.a * vertexDiffuse.a);
    }
    if (technique >= NormalOptim && technique <= NormalMix) {
        vec4 bump = technique == NormalMix ? texture(entSkin2, uv)
                                          : texture(mtlSkin1, uv);
        normalSample = bump;
        albedo = colorSample.rgb;
        float mask = technique == NormalTileSpec ? bump.a : 1.0;
        vec4 result = c4_normal_shade(colorSample, bump, tangentView, tangentSun,
                                      material.ambient, material.skills[0], mask);
        if (collectSunless) {
            sunlessRgb = result.rgb - c4_normal_shade(colorSample, bump, tangentView,
                tangentSun, vec4(0.0), material.skills[0], mask).rgb;
        }
        return result;
    }
    if (technique == NormalTOptShadow || technique == NormalTSpecShadow) {
        vec4 bump = texture(mtlSkin2, uv);
        normalSample = bump;
        albedo = colorSample.rgb;
        float mask = technique == NormalTSpecShadow ? bump.a : 1.0;
        vec4 lightmap = texture(mtlSkin1, uv1);
        vec4 result = c4_normal_shadow_shade(colorSample, bump, lightmap,
            tangentView, tangentSun, material.ambient, material.skills[0], mask);
        if (collectSunless) {
            sunlessRgb = result.rgb - c4_normal_shadow_shade(colorSample, bump, lightmap,
                tangentView, tangentSun, vec4(0.0), material.skills[0], mask).rgb;
        }
        return result;
    }
    if (technique >= CubeNormalOptim && technique <= CubeNormalMix) {
        vec4 bump = technique == CubeNormalMix ? texture(entSkin2, uv)
                                              : texture(mtlSkin2, uv);
        normalSample = bump;
        albedo = colorSample.rgb;
        vec4 cube = texture(mtlSkin1Cube, cubeDirection);
        if (technique == CubeNormalCar) {
            vec4 result = c4_cube_normal_car_shade(colorSample, bump, cube,
                tangentView, tangentSun, material.ambient, material.skills[0],
                material.skills[2].x);
            if (collectSunless) {
                sunlessRgb = result.rgb - c4_cube_normal_car_shade(colorSample, bump, cube,
                    tangentView, tangentSun, vec4(0.0), material.skills[0],
                    material.skills[2].x).rgb;
            }
            return result;
        }
        vec4 result = c4_cube_normal_shade(colorSample, bump, cube, tangentView,
            tangentSun, material.ambient, material.skills[0], material.skills[2].x);
        if (collectSunless) {
            sunlessRgb = result.rgb - c4_cube_normal_shade(colorSample, bump, cube,
                tangentView, tangentSun, vec4(0.0), material.skills[0],
                material.skills[2].x).rgb;
        }
        return result;
    }
    if (technique == EnvCubeMix || technique == EnvCubeAlpha) {
        vec3 cube = texture(mtlSkin1Cube, cubeDirection).rgb;
        if (collectSunless) {
            sunlessRgb = c4FixedEnvironment(colorSample.rgb, sunlessDiffuse, cube,
                                            material.textureFactor.a);
        }
        return vec4(c4FixedEnvironment(colorSample.rgb, vertexDiffuse.rgb, cube,
                                      material.textureFactor.a), colorSample.a);
    }
    if (technique == ShadowMap) {
        vec3 lightmap = texture(mtlSkin1, uv1).rgb;
        if (collectSunless) {
            sunlessRgb = c4FixedShadow(lightmap, sunlessDiffuse, colorSample.rgb);
        }
        return vec4(c4FixedShadow(lightmap, vertexDiffuse.rgb, colorSample.rgb),
                    colorSample.a);
    }
    if (technique >= Terrain4A && technique <= Terrain5C) {
        vec4 tiled = terrainSample(draw.config.x, uv1 * draw.params.xy);
        albedo = tiled.rgb * sourceLighting.state.diffuse.rgb;
        if (collectSunless) sunlessRgb = c4TerrainBase(tiled.rgb, sunlessDiffuse);
        // Ordered draw passes perform the source inverse-alpha blend on UNORM.
        // Overlay alpha comes from the untiled layer and never entity alpha.
        float alpha = draw.config.x == 0 ? tiled.a
                                        : terrainSample(draw.config.x, uv).a;
        return vec4(c4TerrainBase(tiled.rgb, vertexDiffuse.rgb), alpha);
    }
    if (technique == Water) {
        vec2 bumpUV = c4TextureTransform(uv1, material.textureTransform);
        // The authored DXT1 bump texture supplies filtered UNORM R/G directly.
        vec2 sourceDuDv = texture(mtlSkin1, bumpUV).rg;
        vec2 waterUV = c4WaterBumpCoordinates(uv1, sourceDuDv, material.bumpMatrix);
        vec4 waterColor = texture(entSkin1, waterUV);
        albedo = waterColor.rgb * sourceLighting.state.diffuse.rgb;
        if (collectSunless) sunlessRgb = c4FixedWater(waterColor.rgb, sunlessDiffuse);
        return vec4(c4FixedWater(waterColor.rgb, vertexDiffuse.rgb), waterColor.a);
    }
    return colorSample * draw.diffuse;
}

float shadowVisibility(vec3 surfaceNormal) {
    if (quality.sampling.w <= 0.0 || shadow.params.x <= 0.0 ||
        !c4_source_sun_known(sourceLighting.state)) return 1.0;
    float depth = dot(worldPosition - shadow.viewPosition.xyz, shadow.viewForward.xyz);
    int cascade = c4_quality_shadow_cascade(depth, shadow.splits, int(shadow.params.x));
    if (cascade < 0) return 1.0;
    float cosine = dot(c4_pbr_unit(surfaceNormal),
                       c4_pbr_unit(-shadow.sunDirection.xyz));
    return c4_quality_shadow_array_pcf(shadowMaps,
        shadow.matrices[cascade] * vec4(worldPosition, 1.0), float(cascade),
        vec2(shadow.params.y), cosine, shadow.constantBias[cascade], shadow.slopeBias[cascade]);
}

vec3 enhancedPbr(vec3 sourceDirectSrgb, vec3 sourceResidualSrgb,
                 vec4 normalSample, vec3 surfaceNormal, float visibility) {
    uint technique = material.config.x;
    bool normalEffect = technique >= NormalOptim && technique <= CubeNormalMix;
    float metallic = material.modern.x;
    float roughness = material.modern.y;
    float mask = (technique == NormalTileSpec || technique == NormalTSpecShadow)
        ? normalSample.a : 1.0;
    C4SourceLight sun = sourceLighting.state.lights[int(sourceLighting.state.options.w) - 1];
    vec3 normal = normalEffect ? normalSample.rgb * 2.0 - 1.0 : surfaceNormal;
    vec3 view = normalEffect ? tangentView
                            : sourceLighting.state.viewPosition.xzy - worldPosition;
    vec3 light = normalEffect ? tangentSun : -sun.directionType.xzy;
    return c4_quality_source_sun_metallic(sourceDirectSrgb, sourceResidualSrgb,
        metallic, roughness, mask, normal, view, light) * visibility;
}

vec3 environmentFreeResidual(vec4 colorSample, vec4 normalSample, vec3 residual) {
    uint technique = material.config.x;
    if (technique >= CubeNormalOptim && technique <= CubeNormalMix) {
        vec4 result;
        vec4 direct;
        if (technique == CubeNormalCar) {
            result = c4_cube_normal_car_shade(colorSample, normalSample, vec4(0.0),
                tangentView, tangentSun, material.ambient, material.skills[0], material.skills[2].x);
            direct = c4_cube_normal_car_shade(colorSample, normalSample, vec4(0.0),
                tangentView, tangentSun, vec4(0.0), material.skills[0], material.skills[2].x);
        } else {
            result = c4_cube_normal_shade(colorSample, normalSample, vec4(0.0),
                tangentView, tangentSun, material.ambient, material.skills[0], material.skills[2].x);
            direct = c4_cube_normal_shade(colorSample, normalSample, vec4(0.0),
                tangentView, tangentSun, vec4(0.0), material.skills[0], material.skills[2].x);
        }
        return result.rgb - direct.rgb;
    }
    if (technique == EnvCubeMix || technique == EnvCubeAlpha) {
        return clamp(c4FixedEnvironment(colorSample.rgb, sunlessDiffuse, vec3(0.0),
            material.textureFactor.a) + sunlessSpecular, 0.0, 1.0);
    }
    return residual;
}

vec3 modernEnvironment(vec3 albedo, vec3 normal) {
    vec3 view = c4_pbr_unit(frame.viewPosition.xzy - worldPosition);
    vec3 reflection = reflect(-view, normal);
    // Cube data stays in original shader axes; only native directions swap Y/Z.
    vec3 irradiance = textureLod(modernIrradiance, normal.xzy, 0.0).rgb;
    vec3 prefiltered = textureLod(modernPrefiltered, reflection.xzy,
        material.modern.y * material.environment.x).rgb;
    return c4_quality_environment_metallic(albedo, material.modern.x,
        material.modern.y, material.modern.z, normal, view, irradiance, prefiltered);
}

vec3 sourceEncoded(vec3 linear) {
    return clamp(vec3(c4_post_srgb(max(linear.r, 0.0)),
                      c4_post_srgb(max(linear.g, 0.0)),
                      c4_post_srgb(max(linear.b, 0.0))), 0.0, 1.0);
}

void main() {
    vec4 sourceColor = texture(entSkin1, uv);
    vec4 sourceNormal = vec4(0.0);
    vec3 albedo = vec3(0.0);
    vec3 sunlessRgb = vec3(0.0);
    bool fixedFunction = material.config.x == FixedFunction ||
        (material.config.x >= EnvCubeMix && material.config.x <= Water);
    bool pbr = !overlayPass && !effectPass && !hudMesh && !shadowPass && !skyPass &&
        quality.effects.z > 0.0 && material.extra.z > 0.0 &&
        material.config.z != 0 && sourceLighting.state.status.x != 0 &&
        frame.fogParams.w > 0.0 && c4_source_sun_known(sourceLighting.state) &&
        (!fixedFunction || sourceLighting.state.controls.x != 0);
    bool collectSunless = !overlayPass && !effectPass && !hudMesh && !shadowPass && !skyPass &&
        material.config.z != 0 && sourceLighting.state.status.x != 0 &&
        c4_source_sun_known(sourceLighting.state) &&
        (!fixedFunction || sourceLighting.state.controls.x != 0) &&
        (pbr || frame.renderOptions.z > 0.0 ||
         (fixedFunction && sourceLighting.state.controls.x != 0 &&
          quality.sampling.w > 0.0 && shadow.params.x > 0.0));
    bool fixedBaseUV0 = material.config.x == FixedFunction ||
        material.config.x == EnvCubeMix || material.config.x == EnvCubeAlpha;
    bool knownFixedAlpha = !overlayPass && !effectPass &&
        fixedBaseUV0 && sourceLighting.state.options.y != 0 &&
        (sourceLighting.state.options.z == 2 || sourceLighting.state.options.z == 4);
    if (overlayPass) {
        baseColor = sourceColor * draw.diffuse;
    } else if (effectPass) {
        baseColor = sourceColor * vertexDiffuse;
        baseColor.rgb = clamp(baseColor.rgb, 0.0, 1.0);
    } else {
        baseColor = sourceMaterial(sourceColor, sourceNormal, albedo,
                                   collectSunless, sunlessRgb);
        if (knownFixedAlpha) {
            baseColor.a = c4FixedTextureAlpha(sourceColor.a,
                sourceLighting.state.diffuse.a, sourceLighting.state.options.z == 4u);
        }
        if (fixedFunction && material.config.z != 0 &&
            sourceLighting.state.status.x != 0) {
            baseColor.rgb = clamp(baseColor.rgb + vertexSpecular, 0.0, 1.0);
            if (collectSunless) {
                sunlessRgb = clamp(sunlessRgb + sunlessSpecular, 0.0, 1.0);
            } else if (sourceLighting.state.status.z != 0 &&
                       sourceLighting.state.status.w != 0) {
                baseColor = c4_source_apply_fog(sourceLighting.state, baseColor, vertexFog);
            }
        }
    }

    // Color-key roles carry engine-converted alpha before filtering/mipmap generation.
    if ((draw.config.y == 1 || draw.config.y == 2) && baseColor.a < draw.params.w) discard;

    // Known source stage alpha is applied before cutoff, independently of RGB lighting.
    // Only UI and explicitly unknown fixed alpha retain separate entity alpha.
    bool incompleteFixed = !knownFixedAlpha && material.config.x == FixedFunction &&
        (material.config.z == 0 || sourceLighting.state.status.x == 0);
    if (!effectPass && (overlayPass || incompleteFixed)) {
        baseColor.a *= draw.params.z;
    }
    vec3 surfaceNormal = skyPass ? vec3(0.0) : normalize(worldNormal);
    sunOutput = vec4(0.0);
    if (!shadowPass && !overlayPass && !hudMesh) {
        if (pbr && material.config.x >= NormalOptim &&
            material.config.x <= CubeNormalMix) {
            vec3 bump = sourceNormal.rgb * 2.0 - 1.0;
            surfaceNormal = c4_pbr_unit(shadingTangent * bump.x +
                shadingBinormal * bump.y + shadingNormal * bump.z);
        }
        float visibility = (pbr || collectSunless) ? shadowVisibility(surfaceNormal) : 1.0;
        if (pbr) {
            // Replace exactly the authored cube component, not ambient/emissive,
            // lightmaps or non-SUN terms. With no prepared cube keep the source.
            bool ibl = material.environment.y > 0.0;
            // Use the actual complete source SUN difference, including fixed
            // saturation and every FX/lightmap factor, not a new frame radiance.
            vec3 sourceResidual = sunlessRgb;
            vec3 sourceFull = baseColor.rgb;
            if (fixedFunction) {
                sourceResidual = c4_source_apply_fog(sourceLighting.state,
                    vec4(sourceResidual, baseColor.a), vertexFog).rgb;
                sourceFull = c4_source_apply_fog(sourceLighting.state,
                    vec4(sourceFull, baseColor.a), vertexFog).rgb;
            }
            vec3 indirect = ibl ? environmentFreeResidual(sourceColor, sourceNormal, sunlessRgb)
                                : sunlessRgb;
            if (fixedFunction) {
                indirect = c4_source_apply_fog(sourceLighting.state,
                    vec4(indirect, baseColor.a), vertexFog).rgb;
            }
            indirect = c4_quality_srgb_to_linear(max(indirect, vec3(0.0)));
            if (ibl) indirect += modernEnvironment(albedo, surfaceNormal) *
                (fixedFunction ? vertexFog : 1.0);
            vec3 direct = enhancedPbr(sourceFull - sourceResidual, sourceResidual,
                sourceNormal, surfaceNormal, visibility);
            vec3 radiance = indirect + direct;
            baseColor.rgb = quality.effects.w > 0.0 ? radiance : sourceEncoded(radiance);
            sunOutput.rgb = quality.effects.w > 0.0 ? direct
                : baseColor.rgb - sourceEncoded(indirect);
        } else {
            if (quality.effects.w > 0.0) {
                baseColor.rgb = c4_quality_srgb_to_linear(clamp(baseColor.rgb, 0.0, 1.0));
                if (collectSunless) {
                    sunlessRgb = c4_quality_srgb_to_linear(clamp(sunlessRgb, 0.0, 1.0));
                }
            }
            if (collectSunless) sunOutput.rgb = (baseColor.rgb - sunlessRgb) * visibility;
            if (collectSunless) {
                // Remove only the proven SUN contribution; other lights remain intact.
                baseColor.rgb = sunlessRgb + (baseColor.rgb - sunlessRgb) * visibility;
                if (sourceLighting.state.status.z != 0 &&
                    sourceLighting.state.status.w != 0) {
                    vec3 fogColor = sourceLighting.state.fogColor.rgb;
                    if (quality.effects.w > 0.0) {
                        fogColor = c4_quality_srgb_to_linear(fogColor);
                    }
                    baseColor.rgb = mix(fogColor, baseColor.rgb, vertexFog);
                    sunOutput.rgb *= vertexFog;
                }
            }
        }
    }
    positionOutput = skyPass ? vec4(0.0) : vec4(worldPosition, 1.0);
    normalOutput = skyPass ? vec4(0.0) : vec4(surfaceNormal, 1.0);
    bool receiver = !skyPass && !overlayPass && !effectPass && !hudMesh && !shadowPass;
    materialOutput = receiver ? vec4(material.modern.xyz, material.environment.z) : vec4(0.0);
}
