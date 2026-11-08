#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../material_normal.glsl"
#include "../../material_fixed.glsl"
#include "../../source_lighting.glsl"

layout(constant_id = 1) const bool overlayPass = false;
layout(constant_id = 3) const bool effectPass = false;
layout(constant_id = 4) const bool shadowPass = false;
layout(constant_id = 5) const bool hudMesh = false;
layout(constant_id = 6) const bool skyPass = false;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 textureCoordinates;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 secondaryCoordinates;
layout(location = 4) in vec4 sourceTangent;
layout(location = 5) in vec4 sourceColor;

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

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 worldPosition;
layout(location = 2) out vec3 worldNormal;
layout(location = 3) out vec2 uv1;
layout(location = 4) out vec3 tangentView;
layout(location = 5) out vec3 tangentSun;
layout(location = 6) out vec3 cubeDirection;
layout(location = 7) out vec4 vertexDiffuse;
layout(location = 8) out vec3 shadingTangent;
layout(location = 9) out vec3 shadingBinormal;
layout(location = 10) out vec3 shadingNormal;
layout(location = 11) out vec3 vertexSpecular;
layout(location = 12) out float vertexFog;
layout(location = 13) out vec3 sunlessDiffuse;
layout(location = 14) out vec3 sunlessSpecular;

void main() {
    uv1 = secondaryCoordinates;
    tangentView = vec3(0.0);
    tangentSun = vec3(0.0);
    cubeDirection = vec3(0.0);
    vertexDiffuse = draw.diffuse;
    shadingTangent = vec3(0.0);
    shadingBinormal = vec3(0.0);
    shadingNormal = vec3(0.0);
    vertexSpecular = vec3(0.0);
    vertexFog = 1.0;
    sunlessDiffuse = vec3(0.0);
    sunlessSpecular = vec3(0.0);

    if (overlayPass) {
        gl_Position = draw.model * vec4(position, 1.0);
        uv = mix(draw.uvRect.xy, draw.uvRect.zw, textureCoordinates);
        worldPosition = gl_Position.xyz;
        worldNormal = normal;
        return;
    }

    vec4 world = draw.model * vec4(position, 1.0);
    uv = textureCoordinates;
    if (hudMesh) uv *= draw.uvRect.zw;
    worldPosition = world.xyz;
    if (effectPass) {
        vertexDiffuse = sourceColor;
        worldNormal = normal;
        gl_Position = frame.viewProjection * world;
    } else {
        worldNormal = transpose(inverse(mat3(draw.model))) * normal;
        if (hudMesh) worldNormal = normalize(worldNormal);
        gl_Position = frame.viewProjection * world;
        uint technique = material.config.x;
        bool fixedFunction = technique == 0 || (technique >= 10 && technique <= 19);
        if (fixedFunction && material.config.z != 0 &&
            sourceLighting.state.status.x != 0) {
            bool collectSunless = !shadowPass && !hudMesh && !skyPass &&
                sourceLighting.state.controls.x != 0 &&
                ((quality.effects.z > 0.0 && material.extra.z > 0.0 &&
                  frame.fogParams.w > 0.0) ||
                 (quality.sampling.w > 0.0 && shadow.params.x > 0.0) ||
                 frame.renderOptions.z > 0.0) &&
                c4_source_sun_known(sourceLighting.state);
            vertexDiffuse = c4_source_vertex_diffuse_decomposed(sourceLighting.state,
                worldPosition.xzy, worldNormal.xzy, collectSunless,
                vertexSpecular, sunlessDiffuse, sunlessSpecular);
            if (sourceLighting.state.status.z != 0 &&
                sourceLighting.state.status.w != 0) {
                vertexFog = c4_source_vertex_fog(sourceLighting.state, worldPosition.xzy);
            }
        }
        if (material.config.z != 0 && technique >= 1 && technique <= 9) {
            // Engine FX coordinates swap native Y/Z; source tangents are raw D3D.
            const mat4 sourceAxes = mat4(1, 0, 0, 0,
                                        0, 0, 1, 0,
                                        0, 1, 0, 0,
                                        0, 0, 0, 1);
            mat4 sourceWorld = sourceAxes * draw.model * sourceAxes;
            c4_normal_vertex(sourceWorld,
                frame.viewProjection * draw.model * sourceAxes,
                vec4(position.xzy, 1.0), normal.xzy, sourceTangent.xyz,
                frame.viewPosition.xyz, frame.sunDirection.xyz,
                material.skills[1].xyz, gl_Position, tangentView, tangentSun);
            if (!shadowPass && !hudMesh && quality.effects.z > 0.0 && material.extra.z > 0.0 &&
                sourceLighting.state.status.x != 0 &&
                sourceLighting.state.status.y != 0) {
                shadingTangent = (sourceWorld * vec4(sourceTangent.xyz, 0.0)).xzy;
                shadingBinormal = (sourceWorld *
                    vec4(cross(sourceTangent.xyz, normal.xzy), 0.0)).xzy;
                shadingNormal = (sourceWorld * vec4(normal.xzy, 0.0)).xzy;
            }
            if (technique >= 7) {
                cubeDirection = c4_cube_normal_direction(sourceWorld, normal.xzy,
                                                        frame.viewDirection.xyz);
            }
        } else if (material.config.z != 0 && (technique == 10 || technique == 11)) {
            vec3 incident = normalize((frame.viewMatrix *
                vec4(worldPosition.xzy - frame.viewPosition.xyz, 0.0)).xyz);
            vec3 cameraNormal = normalize((frame.viewMatrix *
                vec4(worldNormal.xzy, 0.0)).xyz);
            cubeDirection = c4CubeReflectionCoordinates(incident, cameraNormal,
                                                        material.textureTransform);
        }
    }
    if (shadowPass) {
        gl_Position = shadow.matrices[draw.config.w] * world;
    }
    // Shared cameras retain OpenGL's upward Y and [-w, w] depth range.
    gl_Position.y = -gl_Position.y;
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}
