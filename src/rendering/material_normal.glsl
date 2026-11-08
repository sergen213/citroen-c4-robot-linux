// Recovered equations: work/reports/resources/Normal_*.fx and CubeNormal_*.fx.
// Pure GLSL 1.20/4.50 helpers. Samples are original RGBA, not linearized/PBR.
// All matrices are column-vector equivalents of the HLSL row-vector matrices.
// InPos includes its original w. Directions use w=0; normals are NOT transformed
// by inverse-transpose. UV0 passes through unchanged, as does Golge lightmap UV1.
// TEXCOORD2 supplies the original tangent; no tangent reconstruction is in .fx.
// Unknown engine inputs (ambient RGBA, sun/view directions, tangent generation,
// unspecified sampler filters/address modes) must be supplied, not guessed here.

void c4_normal_vertex(mat4 world, mat4 worldViewProjection, vec4 position,
                      vec3 normal, vec3 tangent, vec3 viewPosition,
                      vec3 sunDirection, vec3 skill5,
                      out vec4 clipPosition, out vec3 tangentView,
                      out vec3 tangentSun)
{
    clipPosition = worldViewProjection * position;
    vec3 worldTangent = (world * vec4(tangent, 0.0)).xyz;
    vec3 worldBinormal = (world * vec4(cross(tangent, normal), 0.0)).xyz;
    vec3 worldNormal = (world * vec4(normal, 0.0)).xyz;
    vec3 view = viewPosition - (world * position).xyz;
    vec3 sun = -(sunDirection + skill5);
    // HLSL builds matrix rows and multiplies matrix * direction. GLSL mat3
    // constructors are columns, so spell out row dot products without transpose.
    tangentView = normalize(vec3(dot(worldTangent, view),
                                 dot(worldBinormal, view),
                                 dot(worldNormal, view)));
    tangentSun = normalize(vec3(dot(worldTangent, sun),
                                dot(worldBinormal, sun),
                                dot(worldNormal, sun)));
}

// CubeNormal_* computes this per VERTEX from engine vecViewDir, not the
// per-fragment tangent view or bump normal. Interpolate and sample as-is.
vec3 c4_cube_normal_direction(mat4 world, vec3 normal, vec3 viewDirection)
{
    vec3 view = normalize(viewDirection);
    vec3 worldNormal = normalize((world * vec4(normal, 0.0)).xyz);
    return reflect(view, worldNormal);
}

// Bump normal is deliberately NOT normalized. Neither is interpolated sun.
// Specular is scalar-broadcast into all four channels, including alpha.
vec4 c4_normal_lighting(vec4 normalSample, vec3 tangentView, vec3 tangentSun,
                        vec4 ambient, vec4 skill1, vec4 sunColor,
                        float specularMask, float lightSpecularMask)
{
    vec3 bumpNormal = 2.0 * normalSample.rgb - vec3(1.0);
    vec4 ambientTerm = skill1.x * ambient;
    vec4 diffuseTerm = skill1.y * clamp(dot(tangentSun, bumpNormal), 0.0, 1.0)
                       * sunColor;
    vec3 reflection = normalize(2.0 * dot(bumpNormal, tangentSun) * bumpNormal
                                - tangentSun);
    float specular = pow(clamp(dot(reflection, normalize(tangentView)), 0.0, 1.0),
                         skill1.w) * skill1.z * specularMask * lightSpecularMask;
    return ambientTerm + diffuseTerm + vec4(specular);
}

// Normal_Optim/Normal_TileOptim/Normal_Mix: specularMask=1.
// Normal_TileSpec: specularMask=normalSample.a.
// entSkin1 uses UV0; normal is mtlSkin1 except Normal_Mix uses entSkin2.
// Both samplers explicitly Wrap U/V. The Tile name introduces NO UV scaling.
vec4 c4_normal_shade(vec4 colorSample, vec4 normalSample,
                     vec3 tangentView, vec3 tangentSun, vec4 ambient,
                     vec4 skill1, float specularMask)
{
    return c4_normal_lighting(normalSample, tangentView, tangentSun, ambient,
                              skill1, vec4(0.9, 0.9, 1.0, 0.9), specularMask, 1.0)
           * colorSample;
}

// Golge: entSkin1 color/mtlSkin2 normal at UV0; mtlSkin1 light at UV1.
// Normal_T_Opt_Golge: specularMask=1; Normal_T_Spc_Golge: normalSample.a.
// The red-light specular factor is signed and MUST NOT be clamped. Full RGBA
// light sample multiplies final color. LightMapSampler specifies no address mode.
vec4 c4_normal_shadow_shade(vec4 colorSample, vec4 normalSample, vec4 lightSample,
                            vec3 tangentView, vec3 tangentSun, vec4 ambient,
                            vec4 skill1, float specularMask)
{
    return c4_normal_lighting(normalSample, tangentView, tangentSun, ambient,
                              skill1, vec4(0.9, 0.9, 1.0, 0.9), specularMask,
                              2.0 * lightSample.r - 1.0) * colorSample * lightSample;
}

// CubeNormal_Optim/Car: normal=mtlSkin2; CubeNormal_Mix: normal=entSkin2.
// Color=entSkin1 and normal samplers explicitly Clamp U/V. Cube=mtlSkin1;
// its address/filter defaults and D3D cube-face orientation are external inputs.
// reflectionWeight=vecSkill9.x; HLSL lerp does NOT clamp this weight.
vec4 c4_cube_normal_shade(vec4 colorSample, vec4 normalSample, vec4 cubeSample,
                          vec3 tangentView, vec3 tangentSun, vec4 ambient,
                          vec4 skill1, float reflectionWeight)
{
    vec4 lighting = c4_normal_lighting(normalSample, tangentView, tangentSun,
                                       ambient, skill1, vec4(0.2, 0.4, 0.5, 0.3), 1.0, 1.0);
    return mix(lighting * colorSample, lighting * cubeSample, reflectionWeight);
}

float c4_cube_normal_fresnel(vec4 normalSample, vec3 tangentView)
{
    vec3 tempNormal = normalize(2.0 * normalSample.rgb - vec3(1.0));
    float cosine = dot(tempNormal, normalize(tangentView));
    // Original pow(cosine,4): explicit integer multiplication preserves the
    // fourth power also for negative cosine (GLSL pow(negative,4) is undefined).
    float cosineSquared = cosine * cosine;
    return 0.5 + (1.0 - cosineSquared * cosineSquared) * 0.5;
}

vec4 c4_cube_normal_car_shade(vec4 colorSample, vec4 normalSample, vec4 cubeSample,
                              vec3 tangentView, vec3 tangentSun, vec4 ambient,
                              vec4 skill1, float reflectionWeight)
{
    return c4_cube_normal_shade(colorSample, normalSample, cubeSample,
                                tangentView, tangentSun, ambient, skill1,
                                reflectionWeight * c4_cube_normal_fresnel(normalSample,
                                                                         tangentView));
}
