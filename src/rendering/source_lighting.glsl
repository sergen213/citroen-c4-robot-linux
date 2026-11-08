#ifndef C4_SOURCE_LIGHTING_GLSL
#define C4_SOURCE_LIGHTING_GLSL

// D3D9 fixed vertex lighting, not an EnhancedPBR approximation. Inputs below
// are the original uploader's D3DLIGHT9/D3DMATERIAL9 binary32 values.
struct C4SourceLight {
    vec4 diffuse, specular, ambient;
    vec4 positionRange, directionType, attenuationFalloff, coneEnabled;
};
struct C4SourceLighting {
    C4SourceLight lights[8];
    mat4 worldToView;
    vec4 viewPosition, globalAmbient, fogColor, fogParams;
    vec4 diffuse, ambient, specular, emissive, materialParams;
#if __VERSION__ >= 130
    uvec4 controls, status, options;
#else
    vec4 controls, status, options;
#endif
};

bool c4_source_sun_known(C4SourceLighting state) {
    float slotPlusOne = float(state.options.w);
    if (float(state.status.x) == 0.0 || slotPlusOne <= 0.0 ||
        slotPlusOne > float(state.status.y) || slotPlusOne > 8.0) return false;
    return state.lights[int(slotPlusOne) - 1].coneEnabled.z != 0.0;
}

// Microsoft D3D9 range test is strictly greater: a light remains enabled at
// exactly Range. Do not replace the reciprocal polynomial with linear fade.
float c4_source_attenuation(C4SourceLight light, float distanceToLight) {
    if (light.directionType.w == 3.0) return 1.0;
    if (distanceToLight > light.positionRange.w) return 0.0;
    vec3 a = light.attenuationFalloff.xyz;
    return 1.0 / (a.x + a.y * distanceToLight + a.z * distanceToLight * distanceToLight);
}
float c4_source_spot(C4SourceLight light, vec3 vertexToLight) {
    if (light.directionType.w != 2.0) return 1.0;
    float rho = dot(normalize(-light.directionType.xyz), vertexToLight);
    float inner = cos(light.coneEnabled.x * 0.5);
    float outer = cos(light.coneEnabled.y * 0.5);
    if (rho >= inner) return 1.0;
    if (rho <= outer) return 0.0;
    return pow((rho - outer) / (inner - outer), light.attenuationFalloff.w);
}

vec4 c4_source_vertex_diffuse_decomposed(C4SourceLighting state, vec3 worldPosition,
                                        vec3 worldNormal, bool collectSunless,
                                        out vec3 fullSpec, out vec3 sunlessDiffuse,
                                        out vec3 sunlessSpec) {
    fullSpec = vec3(0.0);
    sunlessDiffuse = collectSunless ? state.diffuse.rgb : vec3(0.0);
    sunlessSpec = vec3(0.0);
    if (float(state.controls.x) == 0.0) return state.diffuse;
    vec3 viewPosition = (state.worldToView * vec4(worldPosition, 1.0)).xyz;
    vec3 n = mat3(state.worldToView) * worldNormal;
    vec3 diffuse = state.emissive.rgb + state.ambient.rgb * state.globalAmbient.rgb;
    if (collectSunless) sunlessDiffuse = diffuse;
    if (float(state.controls.y) != 0.0) n = normalize(n);
    vec3 eye = float(state.controls.w) != 0.0 ? normalize(-viewPosition) : vec3(0.0, 0.0, 1.0);
    for (int i = 0; i < int(state.status.y); ++i) {
        C4SourceLight light = state.lights[i];
        if (light.coneEnabled.z == 0.0) continue;
        bool directional = light.directionType.w == 3.0;
        vec3 toward = directional ? -light.directionType.xyz
                                  : light.positionRange.xyz - worldPosition;
        float distanceToLight = directional ? 0.0 : length(toward);
        vec3 l = normalize(mat3(state.worldToView) * toward);
        float attenuation = c4_source_attenuation(light, distanceToLight);
        float spot = c4_source_spot(light, normalize(toward));
        float intensity = attenuation * spot;
        float lambert = max(dot(n, l), 0.0);
        vec3 lightAmbient = state.ambient.rgb * light.ambient.rgb;
        vec3 diffuseTerm = intensity * (lightAmbient + state.diffuse.rgb * light.diffuse.rgb * lambert);
        diffuse += diffuseTerm;
        bool sourceSun = false;
        if (collectSunless) {
            sourceSun = float(state.options.w) == float(i + 1);
            sunlessDiffuse += sourceSun ? intensity * lightAmbient : diffuseTerm;
        }
        if (float(state.controls.z) != 0.0 && lambert > 0.0) {
            float nh = max(dot(n, normalize(eye + l)), 0.0);
            float shine = state.materialParams.x == 0.0 ? 1.0 : pow(nh, state.materialParams.x);
            vec3 specularTerm = intensity * state.specular.rgb * light.specular.rgb * shine;
            fullSpec += specularTerm;
            if (collectSunless && !sourceSun) sunlessSpec += specularTerm;
        }
    }
    // D3D diffuse alpha comes from the selected material, not summed lights.
    fullSpec = clamp(fullSpec, 0.0, 1.0);
    if (collectSunless) {
        sunlessDiffuse = clamp(sunlessDiffuse, 0.0, 1.0);
        sunlessSpec = clamp(sunlessSpec, 0.0, 1.0);
    }
    return vec4(clamp(diffuse, 0.0, 1.0), state.diffuse.a);
}

vec4 c4_source_vertex_diffuse(C4SourceLighting state, vec3 worldPosition,
                             vec3 worldNormal, out vec3 vertexSpecular) {
    vec3 sunlessDiffuse, sunlessSpec;
    return c4_source_vertex_diffuse_decomposed(state, worldPosition, worldNormal, false,
                                               vertexSpecular, sunlessDiffuse, sunlessSpec);
}

float c4_source_fog_factor(C4SourceLighting state, float distanceToCamera) {
    if (float(state.status.z) == 0.0 || float(state.status.w) == 0.0) return 1.0;
    if (float(state.status.w) == 1.0) return clamp(exp(-state.fogParams.z * distanceToCamera), 0.0, 1.0);
    if (float(state.status.w) == 2.0) {
        float d = state.fogParams.z * distanceToCamera;
        return clamp(exp(-d*d), 0.0, 1.0);
    }
    return clamp((state.fogParams.y - distanceToCamera) /
                 (state.fogParams.y - state.fogParams.x), 0.0, 1.0);
}
float c4_source_vertex_fog(C4SourceLighting state, vec3 worldPosition) {
    vec3 view = (state.worldToView * vec4(worldPosition, 1.0)).xyz;
    return c4_source_fog_factor(state, float(state.options.x) != 0.0 ? length(view) : view.z);
}
vec4 c4_source_apply_fog(C4SourceLighting state, vec4 color, float vertexFog) {
    return vec4(mix(state.fogColor.rgb, color.rgb, vertexFog), color.a);
}
vec4 c4_source_fog(C4SourceLighting state, vec4 color, float positiveViewDepth) {
    return c4_source_apply_fog(state, color, c4_source_fog_factor(state, positiveViewDepth));
}
#endif
