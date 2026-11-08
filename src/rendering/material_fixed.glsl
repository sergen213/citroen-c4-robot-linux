// Recovered work/reports/resources/{EnvCube_Mix,EnvCube_Alpha,Shadow_Map,
// water_entskin,multi_texture4a-c,multi_texture5a-c}.fx; effect_define.wdl.
// GLSL 1.20-compatible equations only. Inputs are source-space samples and
// actual fixed-function vertex diffuse; no PBR, gamma or invented lighting.

vec3 c4FixedModel(vec3 base, vec3 vertexDiffuse, bool modulate2xCapable)
{
    // 10044d10 model/world-sprite COLOROP5; 100446e0 falls back to4
    // only when the source device lacks its explicit MODULATE2X capability.
    return clamp(base * vertexDiffuse * (modulate2xCapable ? 2.0 : 1.0),
                 0.0, 1.0);
}

vec3 c4FixedEnvironment(vec3 base, vec3 vertexDiffuse, vec3 cube,
                        float textureFactorAlpha)
{
    // Stage 0: MODULATE2X. Stage 1's final assignment is BLENDFACTORALPHA,
    // overriding its earlier BLENDCURRENTALPHA assignment (EnvCube*:31).
    // CURRENT as stage1's second operand is an explicit renderer binding:
    // COLORARG2[1] is absent in the FX, so inherited A7 state is unproven.
    vec3 current = clamp(2.0 * base * vertexDiffuse, 0.0, 1.0);
    return clamp(mix(current, cube, textureFactorAlpha), 0.0, 1.0);
}

vec3 c4FixedShadow(vec3 lightmap, vec3 vertexDiffuse, vec3 base)
{
    // mtlSkin1(UV1) * DIFFUSE, then entSkin1(UV0) * CURRENT * 2.
    vec3 current = clamp(lightmap * vertexDiffuse, 0.0, 1.0);
    return clamp(2.0 * base * current, 0.0, 1.0);
}

vec3 c4FixedWater(vec3 base, vec3 current)
{
    // BUMPENVMAP modifies the next stage's UV, not its current color.
    return clamp(2.0 * base * current, 0.0, 1.0);
}

float c4FixedTextureAlpha(float stage0TextureAlpha, float vertexDiffuseAlpha,
                          bool modulateDiffuse)
{
    // 100446e0/10044d10: opaque SELECTARG1(TEXTURE), transparent MODULATE.
    // Vertex diffuse already contains the source entity/material alpha.
    // Disabled stage-1 alpha retains this result; do not multiply again.
    return clamp(modulateDiffuse
                     ? stage0TextureAlpha * vertexDiffuseAlpha
                     : stage0TextureAlpha,
                 0.0, 1.0);
}

vec2 c4TextureTransform(vec2 uv, mat4 textureTransform)
{
    // D3D COUNT2 uses the third input coordinate for the 2D translation.
    // Input is the column-vector equivalent (transpose) of source matMtl,
    // matching the shared native GPU matrix contract.
    return (textureTransform * vec4(uv, 1.0, 0.0)).xy;
}

vec3 c4CubeReflectionCoordinates(vec3 cameraIncident, vec3 cameraNormal,
                                 mat4 textureTransform)
{
    // cameraIncident points eye -> surface; caller supplies effective unit
    // camera-space normal/incident. matMtl is matViewInv, translation cleared.
    // Input matrix is the column-vector equivalent of that source row matrix.
    return (textureTransform
            * vec4(reflect(cameraIncident, cameraNormal), 0.0)).xyz;
}

vec2 c4WaterBumpCoordinates(vec2 uv1, vec2 bumpSample,
                            vec4 bumpMatrix00_01_10_11)
{
    // Preserve the actual storage encoding: source DXT1 is unsigned RG.
    // Only a genuinely signed bump format supplies signed components.
    // Stage 0 samples transformed UV1; stage 1 perturbs untransformed UV1.
    // D3D BUMPENVMAT offsets use columns, not rows.
    return uv1 + vec2(bumpMatrix00_01_10_11.x * bumpSample.x
                        + bumpMatrix00_01_10_11.z * bumpSample.y,
                      bumpMatrix00_01_10_11.y * bumpSample.x
                        + bumpMatrix00_01_10_11.w * bumpSample.y);
}

vec3 c4TerrainBase(vec3 tiledSample, vec3 vertexDiffuse)
{
    return clamp(tiledSample * vertexDiffuse, 0.0, 1.0);
}

vec3 c4TerrainLayer(vec3 prior, vec3 tiledSample, vec3 vertexDiffuse,
                    float untiledAlpha)
{
    // Same layer sampled twice: RGB at UV1*Skin_Size, alpha at UV0.
    // SRCBLEND=INVSRCALPHA, DESTBLEND=SRCALPHA, in source pass order.
    vec3 source = c4TerrainBase(tiledSample, vertexDiffuse);
    return clamp(mix(source, prior, untiledAlpha), 0.0, 1.0);
}

// Binding contract:
// EnvCube*: one pass, entSkin1 at stage0; cube mtlSkin1 at stage1, clamp U/V,
// CAMERA_SPACE_REFLECTION_VECTOR, COUNT3 matMtl. ZWRITE=true, blend=false
// (Mix) / true (Alpha). No blend factors, alpha ops, filters, cull, depth func
// or remaining stage args are specified by these effects.
// Shadow_Map: one pass; mtlSkin1 UV1 at stage0, entSkin1 UV0 at stage1.
// Neither sampler state, alpha stages nor render states are specified.
// water_entskin: one pass; stage0 mtlSkin1 at transformed UV1, stage1 entSkin1
// at UV1+DU/DV matrix offset. Both linear min/mag/mip; stage1 wraps U/V.
// Lighting=true, cull=none, ZENABLE/ZWRITE=true, blend=false, BlendOp=Add.
// Stage0 address mode, bump matrix and all alpha ops are unspecified.
// multi_texture4/5*: ordered 4/5 passes, p0 entSkin1, p1.. mtlSkin1..3/4.
// All samplers linear min/mag/mip, wrap U/V/W, MaxMipLevel=0 (most detailed
// allowed mip, NOT mipmapping disabled), MipMapLodBias=0. All passes have
// depth test/write=true, cull=none, lighting=true, dither=true, alpha test=false.
// p0 blend=false; p1.. blend=true, src=1-srcAlpha,dst=srcAlpha, clipping=true.
// Ambient=vecLight*10; engine light/material inputs and packed Ambient
// conversion are not supplied by the effects. p0 ALPHAOP0=DISABLE; overlays
// ALPHAOP0=SELECTARG1(texture), ALPHAOP1=DISABLE. Disabled alpha operation
// paired with active color operation is not a proven alpha pipeline default.
// Helpers preserve source RGB equations without claiming unknown alpha parity.
// UV1 tile scales a:90,15,17.5,5; b:120,30,35,10; c:90,60,70,20;
// fifth layer is 130 in all variants. Both terrain matrices default XY identity.
// Effect selection: model_texture_size 0/1/2 -> a/b/c; c4_shadow_size>0 ->5.
// No undocumented depth compare/blend op/separate alpha/specular/fog/sRGB
// state is inferred. Pass quantization/dithering and hardware precision are
// renderer/attachment responsibilities; collapsing passes cannot reproduce
// intermediate UNORM quantization merely by calling these RGB functions.
