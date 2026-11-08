// Recovered work/reports/resources/bloom{Down,H,V,Final}.fx and
// effect_define.wdl:283-474. Pure GLSL 1.20 functions; sampling is caller-owned.

vec4 c4BloomClipPosition(vec2 uv)
{
    // All four source vertex shaders enlarge the screen quad by 0.5%.
    return vec4(uv.x * 2.01 - 1.005, 1.005 - uv.y * 2.01, 0.0, 1.0);
}

vec2 c4BloomDownCoordinate(vec2 uv, int tap)
{
    // Source order: center, +X, +Y, +XY. These are fixed UV offsets,
    // deliberately not a viewport-dependent inverse-resolution kernel.
    if (tap == 1) return uv + vec2(0.01, 0.0);
    if (tap == 2) return uv + vec2(0.0, 0.01);
    if (tap == 3) return uv + vec2(0.01, 0.01);
    return uv;
}

vec4 c4BloomDown(vec4 center, vec4 positiveX, vec4 positiveY,
                 vec4 positiveXY, float highlightThreshold)
{
    vec4 color = center * 0.25;
    color += positiveX * 0.25;
    color += positiveY * 0.25;
    color += positiveXY * 0.25;
    // Keep RGB intact: highlight selection is stored ONLY in alpha.
    color.a = smoothstep(highlightThreshold, 1.0,
                         dot(color.rgb, vec3(0.3, 0.59, 0.11)));
    return color;
}

vec2 c4BloomHCoordinate(vec2 uv, int tap, float blurWidth)
{
    // Despite the name, original bloomH.fx blurs along Y.
    return uv + vec2(0.0, float(tap - 10) * 0.015 * blurWidth);
}

vec2 c4BloomVCoordinate(vec2 uv, int tap, float blurWidth)
{
    // Original bloomV.fx blurs along X.
    return uv + vec2(float(tap - 10) * 0.015 * blurWidth, 0.0);
}

vec4 c4BloomTap(vec4 sampleColor)
{
    // Source: tex2D(...) * (1 - ((i-10)/11)) / 20, int i in [0,20].
    // The integer quotient is zero for every tap. Do not invent abs(),
    // Gaussian weights or normalization: 21 source taps have total gain 1.05.
    // Original Microsoft d3dx9_30.dll compiler 9.12.589.0000 confirms this:
    // ps_2_0 c20.x=0.0500000007, 21 texld samples, one mul and twenty mad.
    return sampleColor / 20.0;
}

vec4 c4BloomComposite(vec4 original, vec4 blur, float highlightIntensity,
                      float sceneIntensity, float glowIntensity)
{
    vec4 color = sceneIntensity * original + glowIntensity * blur
                 + vec4(highlightIntensity * blur.a);
    color.a = 1.0;
    return color;
}

// Binding contract:
// scene_bmap: full selected GA_Cozunurlukler resolution, bmap_createblack(...,32),
// rendered by camView; ordinary camera is hidden. Down/H/V intermediates:
// bmap_createblack(256,256,32), views layers -9/-8/-7, z=-250, clip_far=50.
// Each stage is a render_512.tga entity at (0,0,-200); events admit it only
// when render_view equals the corresponding postprocess view (return0).
// Down mtlSkin1=scene; H mtlSkin1=down; V mtlSkin1=H;
// Final mtlSkin1=V (blurmap), mtlSkin2=scene (basemap), visible screen entity.
// Every declared sampler: linear min/mag/mip, clamp U/V. Down/H/V have
// alpha blend=false, alpha test=false. Final blend=false, ZENABLE=true;
// other render states are not established by these FX files. Source formats
// have 32-bit bmaps; exact engine render target format/rounding remains unknown.
// H/V caller sums c4BloomTap(sample) for tap=0..20 in source order; alpha
// is blurred with RGB, not re-extracted after either pass. Caller samples at
// c4BloomDownCoordinate for tap=0..3; no brightness multiplication of RGB.
// Defaults: threshold .280, width .188, highlight .469, scene .516, glow .094.
// Skills map down.skill1=threshold, H/V.skill2=width, final.skill3=highlight,
// final.skill4=scene, final.skill5=glow (vecSkill5.x, NOT vecSkill1.x).
// These equations have no gamma or color-space conversion. Source changes
// the display gamma ramp separately when entering/leaving bloom, not here.
// new_color.wdl:39-80 builds 256-entry display ramps: default[i] is
// clamp(i+GA_Parlaklik,0,255)*256; corrected RGB uses one shared 256-byte
// lookup followed by 256 indices per channel from rca0.rc4 (defBloom_0) or
// rca1.rc4 (defBLOOM_1), then adds brightness/clamps/multiplies by 256.
// Bloom's off-build still applies corrected ramps on start/acil and default
// ramps on kapan. On-build toggles both the view chain and these same ramps.
// Default brightness=0; menu +/-5 clamps [-50,50], rebuilding and immediately
// applying the DEFAULT ramp. Do not substitute a gamma exponent or apply this
// display transformation before bloom. C4.wdl master_vol is AUDIO volume.
// Runtime lifecycle: ana_menu:2991 starts, :3044 re-enables after reload;
// c4_yaris_start:78,231 re-enable iff bloom_fx_ok==0; game_over:1084,1697
// disable. Shader-version fallback is not an observed bloom gate; defBLOOM_1
// is its compile-time view-chain switch. effect_define:476-606 is commented.
