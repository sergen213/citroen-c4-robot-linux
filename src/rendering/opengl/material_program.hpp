#pragma once

#include "core/render_scene.hpp"
#include "assets/GpuImage.hpp"
#include "MaterialShaders.hpp"
#include "rendering/SourceLighting.hpp"
#include "rendering/opengl/gl.hpp"
#include "rendering/opengl/quality_material.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <span>

namespace c4 {
namespace {

class OriginalMaterialProgram {
    using Technique = rendering::MaterialTechnique;
    using Matrix = std::array<float, 16>;
    InspectionGL& gl_;
    GLuint program_{};
    std::optional<QualityMaterialUniforms> quality_;
    const rendering::RenderScene* scene_{};
    const rendering::Instance* transformed_instance_{};
    bool fixed_normal_uploaded_{};
    Matrix camera_{};
    rendering::NativeGameVisualState visuals_{};
    bool visuals_known_{};
    rendering::SourceLightingState source_lighting_{};
    bool source_lighting_known_{};
    std::optional<rendering::SourceLightingDeviceState> lighting_device_;
    bool incomplete_source_state_{};
    std::array<std::array<GLint, 7>, 8> light_locations_{};
    std::array<GLint, 13> lighting_locations_{};
    static constexpr std::array<const char*, 13> lighting_names_{
        "worldToView", "viewPosition", "globalAmbient", "fogColor", "fogParams",
        "diffuse", "ambient", "specular", "emissive", "materialParams", "controls", "status", "options"
    };

    enum Uniform : std::size_t {
        TechniqueId, NativeModel, ShaderWorld, ShaderWvp, WorldToView,
        ViewPosition, ViewDirection, SunDirection, SourceAmbient, Skill1, Skill5,
        ReflectionWeight, TextureTransform, TextureFactorAlpha, BumpMatrix,
        InspectionTint, InspectionImage, FixedNormalMatrix, Textured, FixedAlphaModulate, PassIndex, PassScale,
        OverlayMode, OverlayReference, BaseImage, NormalImage, LightImage, CubeImage,
        BumpImage, TerrainImage, UniformCount
    };
    std::array<GLint, UniformCount> locations_{};
    static constexpr std::array<const char*, UniformCount> names_{
        "technique", "nativeModel", "shaderWorld", "shaderWvp", "worldToView",
        "viewPosition", "viewDirection", "sunDirection", "sourceAmbient", "skill1", "skill5",
        "reflectionWeight", "textureTransform", "textureFactorAlpha", "bumpMatrix",
        "inspectionTint", "inspectionImage", "fixedNormalMatrix", "textured", "fixedAlphaModulate", "passIndex", "passScale",
        "overlayMode", "overlayReference", "baseImage", "normalImage", "lightImage", "cubeImage",
        "bumpImage", "terrainImage"
    };

    void matrix(Uniform uniform, const Matrix& value) const {
        gl_.glUniformMatrix4fv(locations_[uniform], 1, GL_FALSE, value.data());
    }
    void integer(Uniform uniform, int value) const { gl_.glUniform1i(locations_[uniform], value); }
    void scalar(Uniform uniform, float value) const { gl_.glUniform1f(locations_[uniform], value); }
    [[noreturn]] static void missing(const rendering::Instance& instance, const char* input) {
        throw std::runtime_error("Original OpenGL material '" + instance.material + "' (" +
                                 instance.source + "): missing critical original input: " + input);
    }

    void bind_lighting(const rendering::SourceLightingUniforms& state) const {
        constexpr std::array members{
            &rendering::SourceLight::diffuse, &rendering::SourceLight::specular,
            &rendering::SourceLight::ambient, &rendering::SourceLight::position_range,
            &rendering::SourceLight::direction_type, &rendering::SourceLight::attenuation_falloff,
            &rendering::SourceLight::cone_enabled
        };
        for (std::size_t light = 0; light < state.lights.size(); ++light)
            for (std::size_t field = 0; field < members.size(); ++field)
                gl_.glUniform4fv(light_locations_[light][field], 1, (state.lights[light].*members[field]).data());
        gl_.glUniformMatrix4fv(lighting_locations_[0], 1, GL_FALSE, state.world_to_view.data());
        const std::array vectors{&state.view_position, &state.global_ambient, &state.fog_color,
            &state.fog_params, &state.diffuse, &state.ambient, &state.specular, &state.emissive,
            &state.material_params};
        for (std::size_t i = 0; i < vectors.size(); ++i)
            gl_.glUniform4fv(lighting_locations_[i + 1], 1, vectors[i]->data());
        const std::array flags{&state.controls, &state.status, &state.options};
        for (std::size_t i = 0; i < flags.size(); ++i) {
            std::array<float, 4> values{};
            for (unsigned channel = 0; channel < 4; ++channel)
                values[channel] = static_cast<float>((*flags[i])[channel]);
            gl_.glUniform4fv(lighting_locations_[i + 10], 1, values.data());
        }
    }

    static std::string vertex_source() {
        return std::string("#version 120\n") + rendering::normal_shader + rendering::fixed_shader +
            rendering::source_lighting_shader + R"GLSL(
attribute vec3 position;
attribute vec2 uv;
attribute vec3 normal;
attribute vec2 secondaryUV;
attribute vec4 tertiaryUV;
uniform int technique;
uniform mat4 nativeModel, shaderWorld, shaderWvp, worldToView, textureTransform, fixedNormalMatrix;
uniform vec3 viewPosition, viewDirection, sunDirection, skill5;
uniform vec4 inspectionTint;
uniform bool inspectionImage;
uniform bool qmEnhanced, qmPbr, qmDirectKnown;
uniform int qmCascadeCount;
uniform C4SourceLighting sourceLighting;
varying vec2 uv0, uv1;
varying vec3 nativeWorldPosition, nativeWorldNormal, tangentView, tangentSun, cubeDirection;
varying vec4 vertexDiffuse;
varying vec3 vertexSpecular;
varying vec3 sourceIndirectDiffuse, sourceIndirectSpecular;
varying float vertexFog;
void main() {
    vec4 sourcePosition = vec4(position.xzy, 1.0);
    vec3 sourceNormal = normal.xzy;
    vec4 nativeWorld = nativeModel * vec4(position, 1.0);
    nativeWorldPosition = nativeWorld.xyz;
    nativeWorldNormal = (nativeModel * vec4(normal, 0.0)).xyz;
    uv0 = uv;
    uv1 = secondaryUV;
    tangentView = vec3(0.0);
    tangentSun = vec3(0.0);
    cubeDirection = vec3(0.0);
    vertexDiffuse = inspectionTint;
    vertexSpecular = vec3(0.0);
    sourceIndirectDiffuse = vec3(0.0);
    sourceIndirectSpecular = vec3(0.0);
    vertexFog = 1.0;
    if ((technique == 0 || technique >= 10) && !inspectionImage) {
        nativeWorldNormal = (fixedNormalMatrix * vec4(normal, 0.0)).xyz;
        bool collectIndirect = qmEnhanced && qmDirectKnown && (qmPbr || qmCascadeCount > 0);
        vertexDiffuse = c4_source_vertex_diffuse_decomposed(sourceLighting, nativeWorldPosition.xzy,
            nativeWorldNormal.xzy, collectIndirect, vertexSpecular, sourceIndirectDiffuse,
            sourceIndirectSpecular);
        vertexFog = c4_source_vertex_fog(sourceLighting, nativeWorldPosition.xzy);
    }
    gl_Position = shaderWvp * sourcePosition;
    if (technique >= 1 && technique <= 9) {
        // TEXCOORD2 is already in D3D shader axes: do not swap its xyz.
        c4_normal_vertex(shaderWorld, shaderWvp, sourcePosition, sourceNormal,
                         tertiaryUV.xyz, viewPosition, sunDirection, skill5,
                         gl_Position, tangentView, tangentSun);
        if (technique >= 7)
            cubeDirection = c4_cube_normal_direction(shaderWorld, sourceNormal, viewDirection);
    } else if (technique == 10 || technique == 11) {
        vec3 incident = normalize((worldToView *
            vec4(nativeWorldPosition.xzy - viewPosition, 0.0)).xyz);
        vec3 cameraNormal = normalize((worldToView *
            vec4(nativeWorldNormal.xzy, 0.0)).xyz);
        cubeDirection = c4CubeReflectionCoordinates(incident, cameraNormal, textureTransform);
    }
}
)GLSL";
    }

    static std::string fragment_source() {
        return std::string("#version 120\n") + rendering::normal_shader + rendering::fixed_shader +
            rendering::source_lighting_shader + quality_material_glsl() + R"GLSL(
uniform int technique, passIndex, overlayMode;
uniform bool textured, fixedAlphaModulate, inspectionImage;
uniform sampler2D baseImage, normalImage, lightImage, bumpImage, terrainImage;
uniform samplerCube cubeImage;
uniform vec4 sourceAmbient, skill1, bumpMatrix;
uniform mat4 textureTransform;
uniform vec2 passScale;
uniform float reflectionWeight, textureFactorAlpha;
uniform float overlayReference;
uniform C4SourceLighting sourceLighting;
varying vec2 uv0, uv1;
varying vec3 nativeWorldPosition, nativeWorldNormal, tangentView, tangentSun, cubeDirection;
varying vec4 vertexDiffuse;
varying vec3 vertexSpecular;
varying vec3 sourceIndirectDiffuse, sourceIndirectSpecular;
varying float vertexFog;
void main() {
    vec4 baseSample = textured ? texture2D(baseImage, uv0) : vec4(1.0);
    vec4 normalSample = vec4(0.0);
    vec4 cubeSample = vec4(0.0);
    vec4 result;
    vec3 sourceDirectSrgb = vec3(0.0);
    vec3 indirectResult = vec3(0.0);
    bool needDirect = qmEnhanced && qmDirectKnown && (qmPbr || qmCascadeCount > 0);
    if (technique >= 1 && technique <= 9) {
        normalSample = texture2D(normalImage, uv0);
        float mask = (technique == 3 || technique == 6) ? normalSample.a : 1.0;
        if (technique == 5 || technique == 6) {
            vec4 lightSample = texture2D(lightImage, uv1);
            result = c4_normal_shadow_shade(baseSample, normalSample, lightSample,
                tangentView, tangentSun, sourceAmbient, skill1, mask);
            if (needDirect)
                sourceDirectSrgb = c4_normal_shadow_shade(baseSample, normalSample, lightSample,
                    tangentView, tangentSun, vec4(0.0), skill1, mask).rgb;
        } else if (technique >= 7) {
            cubeSample = textureCube(cubeImage, cubeDirection);
            float weight = reflectionWeight;
            if (technique == 8) {
                result = c4_cube_normal_car_shade(baseSample, normalSample, cubeSample,
                    tangentView, tangentSun, sourceAmbient, skill1, weight);
                if (needDirect)
                    sourceDirectSrgb = c4_cube_normal_car_shade(baseSample, normalSample, cubeSample,
                        tangentView, tangentSun, vec4(0.0), skill1, weight).rgb;
            } else {
                result = c4_cube_normal_shade(baseSample, normalSample, cubeSample,
                    tangentView, tangentSun, sourceAmbient, skill1, weight);
                if (needDirect)
                    sourceDirectSrgb = c4_cube_normal_shade(baseSample, normalSample, cubeSample,
                        tangentView, tangentSun, vec4(0.0), skill1, weight).rgb;
            }
        } else {
            result = c4_normal_shade(baseSample, normalSample, tangentView, tangentSun,
                                    sourceAmbient, skill1, mask);
            if (needDirect)
                sourceDirectSrgb = c4_normal_shade(baseSample, normalSample, tangentView, tangentSun,
                                                 vec4(0.0), skill1, mask).rgb;
        }
    } else if (technique == 10 || technique == 11) {
        cubeSample = textureCube(cubeImage, cubeDirection);
        result = vec4(c4FixedEnvironment(baseSample.rgb, vertexDiffuse.rgb,
                                         cubeSample.rgb, textureFactorAlpha),
                      c4FixedTextureAlpha(baseSample.a, vertexDiffuse.a, fixedAlphaModulate));
        if (needDirect)
            indirectResult = c4FixedEnvironment(baseSample.rgb, sourceIndirectDiffuse,
                                                cubeSample.rgb, textureFactorAlpha);
    } else if (technique == 12) {
        vec4 lightSample = texture2D(lightImage, uv1);
        result = vec4(c4FixedShadow(lightSample.rgb, vertexDiffuse.rgb, baseSample.rgb),
                      c4FixedTextureAlpha(lightSample.a, vertexDiffuse.a, fixedAlphaModulate));
        if (needDirect)
            indirectResult = c4FixedShadow(lightSample.rgb, sourceIndirectDiffuse, baseSample.rgb);
    } else if (technique >= 13 && technique <= 18) {
        vec4 tiled = texture2D(terrainImage, uv1 * passScale);
        float alpha = passIndex == 0 ? tiled.a : texture2D(terrainImage, uv0).a;
        // Source framebuffer blending preserves intermediate pass quantization.
        result = vec4(c4TerrainBase(tiled.rgb, vertexDiffuse.rgb), alpha);
        baseSample = tiled;
        if (needDirect) indirectResult = c4TerrainBase(tiled.rgb, sourceIndirectDiffuse);
    } else if (technique == 19) {
        // Original mtlSkin1 is ordinary DXT1. Pinned DXVK/Wine sample UNORM RG
        // directly for BUMPENVMAP; historical original hardware is unobserved.
        vec4 bumpSample = texture2D(bumpImage, c4TextureTransform(uv1, textureTransform));
        vec2 waterUV = c4WaterBumpCoordinates(uv1, bumpSample.rg, bumpMatrix);
        baseSample = texture2D(baseImage, waterUV);
        result = vec4(c4FixedWater(baseSample.rgb, vertexDiffuse.rgb),
                      c4FixedTextureAlpha(bumpSample.a, vertexDiffuse.a, fixedAlphaModulate));
        if (needDirect) indirectResult = c4FixedWater(baseSample.rgb, sourceIndirectDiffuse);
    } else {
        result = vec4(c4FixedModel(baseSample.rgb, vertexDiffuse.rgb, !inspectionImage),
                      c4FixedTextureAlpha(baseSample.a, vertexDiffuse.a, fixedAlphaModulate));
        // Source material vertex diffuse already folds TRANSLUCENT entity alpha once.
        if (needDirect)
            indirectResult = c4FixedModel(baseSample.rgb, sourceIndirectDiffuse, !inspectionImage);
    }
    if (technique == 0 || technique >= 10) {
        result.rgb = clamp(result.rgb + vertexSpecular, 0.0, 1.0);
        if (needDirect) {
            indirectResult = clamp(indirectResult + sourceIndirectSpecular, 0.0, 1.0);
            // Retain authored ambient/emissive/cube and other lights; fog attenuates
            // only the source sun difference. Its alpha is never changed here.
            sourceDirectSrgb = (result.rgb - indirectResult) * vertexFog;
        }
        result = c4_source_apply_fog(sourceLighting, result, vertexFog);
    }
    // D3D ALPHATEST compares the actual final source pixel shader alpha.
    if (overlayMode == 1 && result.a < overlayReference) discard;
    vec3 nativeResidualSrgb = vec3(0.0);
    if (qmPbr) {
        nativeResidualSrgb = result.rgb - sourceDirectSrgb;
        if (technique >= 7 && technique <= 9) {
            // Original full RGBA and cutoff above still use authored skill9.
            float weight = qmReflectivity;
            if (technique == 8) weight *= c4_cube_normal_fresnel(normalSample, tangentView);
            nativeResidualSrgb = skill1.x * sourceAmbient.rgb
                * mix(baseSample.rgb, cubeSample.rgb, weight);
        } else if (technique == 10 || technique == 11) {
            vec3 residual = clamp(c4FixedEnvironment(baseSample.rgb, sourceIndirectDiffuse,
                cubeSample.rgb, qmReflectivity) + sourceIndirectSpecular, 0.0, 1.0);
            nativeResidualSrgb = c4_source_apply_fog(sourceLighting,
                vec4(residual, result.a), vertexFog).rgb;
        }
    }
    result = c4_apply_quality(result, normalSample,
        nativeWorldPosition, nativeWorldNormal, tangentView, tangentSun,
        sourceDirectSrgb, nativeResidualSrgb);
    gl_FragColor = result;
}
)GLSL";
    }

public:
    explicit OriginalMaterialProgram(InspectionGL& gl) : gl_(gl) {
        const auto vertex_text = vertex_source();
        const auto fragment_text = fragment_source();
        InspectionShader vertex{gl_, compile_inspection_shader(gl_, GL_VERTEX_SHADER, vertex_text.c_str())};
        InspectionShader fragment{gl_, compile_inspection_shader(gl_, GL_FRAGMENT_SHADER, fragment_text.c_str())};
        program_ = gl_.glCreateProgram();
        if (!program_) throw std::runtime_error("OpenGL could not allocate original material program");
        try {
            gl_.glAttachShader(program_, vertex.id);
            gl_.glAttachShader(program_, fragment.id);
            gl_.glBindAttribLocation(program_, 0, "position");
            gl_.glBindAttribLocation(program_, 1, "uv");
            gl_.glBindAttribLocation(program_, 2, "normal");
            gl_.glBindAttribLocation(program_, 3, "secondaryUV");
            gl_.glBindAttribLocation(program_, 4, "tertiaryUV");
            gl_.glLinkProgram(program_);
            GLint linked = GL_FALSE;
            gl_.glGetProgramiv(program_, GL_LINK_STATUS, &linked);
            if (!linked) {
                std::array<char, 4096> message{};
                gl_.glGetProgramInfoLog(program_, static_cast<GLsizei>(message.size()), nullptr, message.data());
                throw std::runtime_error("Original OpenGL material shader link: " + std::string(message.data()));
            }
            for (std::size_t i = 0; i < names_.size(); ++i)
                locations_[i] = gl_.glGetUniformLocation(program_, names_[i]);
            constexpr std::array<const char*, 7> light_fields{
                "diffuse", "specular", "ambient", "positionRange", "directionType", "attenuationFalloff", "coneEnabled"
            };
            for (std::size_t light = 0; light < light_locations_.size(); ++light)
                for (std::size_t field = 0; field < light_fields.size(); ++field) {
                    const auto name = "sourceLighting.lights[" + std::to_string(light) + "]." + light_fields[field];
                    light_locations_[light][field] = gl_.glGetUniformLocation(program_, name.c_str());
                }
            for (std::size_t i = 0; i < lighting_names_.size(); ++i) {
                const auto name = std::string("sourceLighting.") + lighting_names_[i];
                lighting_locations_[i] = gl_.glGetUniformLocation(program_, name.c_str());
            }
            quality_.emplace(gl_, program_);
            use();
            for (int unit = 0; unit < 6; ++unit)
                integer(static_cast<Uniform>(BaseImage + unit), unit);
        } catch (...) {
            gl_.glDeleteProgram(program_);
            program_ = 0;
            throw;
        }
    }
    ~OriginalMaterialProgram() { if (program_) gl_.glDeleteProgram(program_); }
    OriginalMaterialProgram(const OriginalMaterialProgram&) = delete;
    OriginalMaterialProgram& operator=(const OriginalMaterialProgram&) = delete;

    GLuint program() const noexcept { return program_; }
    bool qualitySupport() const noexcept { return program_ != 0 && quality_.has_value(); }
    bool pbr_active() const noexcept { return quality_ && quality_->pbr_active; }
    bool incomplete_source_state() const noexcept { return incomplete_source_state_; }
    void use() const { gl_.glUseProgram(program_); }

    void set_lighting_device(const rendering::SourceLightingDeviceState& device) {
        lighting_device_ = device;
    }

    void bind_frame(const rendering::FrameSnapshot& frame) {
        use();
        scene_ = &frame.scene;
        camera_ = frame.camera.view_projection;
        transformed_instance_ = nullptr;
        fixed_normal_uploaded_ = false;
        visuals_known_ = frame.visuals != nullptr;
        visuals_ = frame.visuals ? *frame.visuals : rendering::NativeGameVisualState{};
        source_lighting_known_ = frame.source_lighting != nullptr;
        source_lighting_ = frame.source_lighting ? *frame.source_lighting : rendering::SourceLightingState{};
        gl_.glUniform3fv(locations_[ViewPosition], 1, visuals_.view_position.data());
        gl_.glUniform3fv(locations_[ViewDirection], 1, visuals_.view_direction.data());
        gl_.glUniform3fv(locations_[SunDirection], 1, visuals_.sun_direction.data());
        matrix(WorldToView, visuals_.world_to_view);
        bind_quality_frame(nullptr, nullptr);
    }

    void bind_sky_camera(const rendering::CameraState& camera,
                         const rendering::quality::OpenGLQuality* quality) {
        camera_ = camera.view_projection;
        transformed_instance_ = nullptr;
        fixed_normal_uploaded_ = false;
        // Keep target HDR encoding, but sky is neither shadowed nor PBR-lit.
        bind_quality_frame(quality, nullptr);
        quality_->pbr_enabled = false;
    }

    void bind_quality_frame(const rendering::quality::OpenGLQuality* quality,
                            const rendering::quality::ShadowCascades* cascades,
                            const std::array<float, 4>* nativePositiveDepthRow = nullptr) {
        use();
        quality_->bind_frame(gl_, quality, cascades, visuals_, nativePositiveDepthRow);
    }

    rendering::RasterState bind_instance(const rendering::Instance& instance, const rendering::MaterialPass& pass,
                                         std::size_t passIndex, const assets::GpuImage* overlayImage,
                                         const rendering::OriginalModelAlphaDecision* alpha,
                                         const rendering::OriginalMaterialDrawState& draw,
                                         bool sourceEffectsEnabled,
                                         std::span<const rendering::quality::ModernMaterialOverride> overrides = {}) {
        if (!scene_) throw std::runtime_error("Original OpenGL material requires bind_frame before bind_instance");
        const auto& state = instance.material_state;
        const auto technique = draw.technique;
        if (technique == Technique::Unknown) missing(instance, "unrecovered effect technique");
        if (instance.mesh_index >= scene_->meshes.size()) missing(instance, "valid source mesh index");
        if (passIndex >= draw.passes.size()) missing(instance, "valid effective original material pass index");
        auto effectiveRaster = pass.raster;
        const bool spriteNoDepthWrite = instance.source_sprite && instance.source_sprite_no_depth_write;
        if (alpha) {
            if (spriteNoDepthWrite) {
                auto spriteAlpha = *alpha;
                spriteAlpha.depth_write = false;
                effectiveRaster = rendering::apply_original_model_alpha(pass.raster, spriteAlpha);
            } else {
                effectiveRaster = rendering::apply_original_model_alpha(pass.raster, *alpha);
            }
        } else if (spriteNoDepthWrite && (pass.raster.effect_override_mask & rendering::DepthWrite) == 0) {
            effectiveRaster.depth_write = false;
        }
        const auto& mesh = scene_->meshes[instance.mesh_index];
        const bool normalTechnique = rendering::material_requires_tangent(technique);
        const bool requiredTexturesKnown = rendering::material_has_required_textures(state, sourceEffectsEnabled);
        // Native RGB approximation needs actual inputs, not proven Original
        // inherited raster state or bit-identical texture implementation.
        bool qualitySourceComplete = requiredTexturesKnown &&
            (!normalTechnique || state.ambient_alpha_known) &&
            (!instance.source_sprite || instance.source_sprite_state_known);
        incomplete_source_state_ = !qualitySourceComplete || !effectiveRaster.defaults_recovered;
        const bool terrain = technique >= Technique::Terrain4A && technique <= Technique::Terrain5C;
        if (rendering::material_requires_uv1(technique) && mesh.secondary_uv.size() != mesh.vertices.size())
            missing(instance, "original TEXCOORD1 (UV1)");
        if (normalTechnique) {
            if (mesh.tertiary_uv.size() != mesh.vertices.size() ||
                (mesh.tangent_provenance != assets::TangentProvenance::ImportedSource &&
                 mesh.tangent_provenance != assets::TangentProvenance::SourceAlgorithmNativePort))
                missing(instance, "source-provenance TEXCOORD2 D3D tangent (imported or recovered source algorithm)");
            if (!visuals_known_ || !visuals_.view_inputs_known)
                missing(instance, "original shader-space camera/sun inputs");
            if ((state.skills_known_mask & 0x7fu) != 0x7fu)
                missing(instance, "source MATERIAL skill1..7 values (unassigned fields are not known zero)");
            if ((state.color_known_mask & 0x7u) != 0x7u)
                missing(instance, "source MATERIAL ambient RGB");
            if (technique >= Technique::CubeNormalOptim && (state.skills_known_mask & (1u << 8)) == 0)
                missing(instance, "source MATERIAL skill9 reflection weight");
        }
        if (technique != Technique::FixedFunction && !requiredTexturesKnown)
            missing(instance, "source material texture roles");
        if (technique == Technique::EnvCubeMix || technique == Technique::EnvCubeAlpha) {
            if (!state.texture_factor_known) missing(instance, "original pixel_for_vec packed texture factor");
            if (!visuals_known_ || !visuals_.view_inputs_known)
                missing(instance, "original camera-space reflection inputs");
            bool nonzeroTransform = false;
            for (float value : state.texture_transform) {
                if (!std::isfinite(value)) missing(instance, "finite original matMtl environment view transform");
                nonzeroTransform = nonzeroTransform || value != 0.0f;
            }
            if (!nonzeroTransform) missing(instance, "original matMtl environment view callback transform");
        }
        if (technique == Technique::Water) {
            if (!state.bump_matrix_known) missing(instance, "original BUMPENVMAT00/01/10/11");
            incomplete_source_state_ = true; // Compatible DXT1 bump semantics, not hardware parity proof.
            qualitySourceComplete = false;
        }
        const bool inspectionImage = instance.kind == rendering::InstanceKind::Image &&
            !instance.source_entity && !scene_->world;
        std::optional<rendering::SourceLightingUniforms> lighting;
        if (!normalTechnique && !inspectionImage) {
            if (!source_lighting_known_) missing(instance, "actual engine fixed-function lighting state");
            lighting = rendering::source_lighting_uniforms(source_lighting_, state, instance.flags, alpha,
                                                          instance.source_lighting_cache, &draw,
                                                          lighting_device_ ? &*lighting_device_ : nullptr);
            if (lighting->options[1] == 0) {
                incomplete_source_state_ = true;
                qualitySourceComplete = false;
            }
            if (lighting->status[0] == 0)
                missing(instance, "actual engine fixed-function lighting/material/fog conversion");
        }

        int overlayMode = 0;
        if (overlayImage && overlayImage->implementation_precision_fallback)
            incomplete_source_state_ = true;
        if (!terrain && (instance.flags & (1u << 12)) != 0 &&
            effectiveRaster.alpha_comparison == rendering::AlphaComparison::GreaterEqual) {
            if (!overlayImage || !overlayImage->source_alpha_known)
                missing(instance, "canonical first-upload bitmap alpha metadata for OVERLAY");
            // The canonical upload already contains the actual first-upload key
            // conversion, if any. Never test filtered RGB against a guessed key.
            overlayMode = 1;
        }
        if (normalTechnique && !state.ambient_alpha_known && (overlayMode == 1 || effectiveRaster.blend))
            missing(instance, "original matAmbient.w required by source shader alpha");
        if (!normalTechnique && !inspectionImage && !terrain &&
            (!alpha || !lighting || lighting->options[1] == 0) &&
            (overlayMode == 1 || effectiveRaster.blend))
            missing(instance, "original draw-time material/view/bitmap alpha decision");
        use();
        if (lighting) bind_lighting(*lighting);
        integer(InspectionImage, inspectionImage);
        if (transformed_instance_ != &instance) {
            constexpr std::array<unsigned, 4> axes{0, 2, 1, 3};
            Matrix shaderWorld{}, shaderWvp{};
            const auto nativeWvp = quality_matrix_multiply(camera_, instance.model_to_world);
            for (unsigned column = 0; column < 4; ++column)
                for (unsigned row = 0; row < 4; ++row) {
                    shaderWorld[column * 4 + row] = instance.model_to_world[axes[column] * 4 + axes[row]];
                    shaderWvp[column * 4 + row] = nativeWvp[axes[column] * 4 + row];
                }
            matrix(NativeModel, instance.model_to_world);
            matrix(ShaderWorld, shaderWorld);
            matrix(ShaderWvp, shaderWvp);
            matrix(TextureTransform, state.texture_transform);
            transformed_instance_ = &instance;
            fixed_normal_uploaded_ = false;
        }
        if (!normalTechnique && !inspectionImage && !fixed_normal_uploaded_) {
            const auto inverse = quality_matrix_inverse(instance.model_to_world);
            if (!inverse) missing(instance, "invertible fixed-function world normal transform");
            Matrix normalMatrix{};
            for (unsigned column = 0; column < 4; ++column)
                for (unsigned row = 0; row < 4; ++row)
                    normalMatrix[column * 4 + row] = (*inverse)[row * 4 + column];
            matrix(FixedNormalMatrix, normalMatrix);
            fixed_normal_uploaded_ = true;
        }
        integer(TechniqueId, static_cast<int>(technique));
        integer(Textured, state.entity_skins[0].has_value() || instance.texture_index.has_value());
        integer(FixedAlphaModulate, alpha && alpha->modulate_diffuse);
        integer(PassIndex, static_cast<int>(passIndex));
        integer(OverlayMode, overlayMode);
        scalar(OverlayReference, effectiveRaster.alpha_reference);
        gl_.glUniform2fv(locations_[PassScale], 1, pass.uv_scale.data());
        gl_.glUniform4fv(locations_[SourceAmbient], 1, state.ambient.data());
        gl_.glUniform4fv(locations_[Skill1], 1, state.skills.data());
        gl_.glUniform3fv(locations_[Skill5], 1, state.skills.data() + 4);
        scalar(ReflectionWeight, state.skills[8]);
        scalar(TextureFactorAlpha, static_cast<float>(state.texture_factor >> 24) / 255.0f);
        gl_.glUniform4fv(locations_[BumpMatrix], 1, state.bump_environment_matrix.data());
        // Neutral inspection tint belongs only to image inspection, never source lighting.
        gl_.glUniform4fv(locations_[InspectionTint], 1, instance.diffuse.data());
        const auto sunSlotPlusOne = lighting ? lighting->options[3] : 0u;
        const bool fixedDirectKnown = lighting && lighting->status[0] != 0 && lighting->controls[0] != 0 &&
            sunSlotPlusOne != 0 && sunSlotPlusOne <= lighting->status[1] &&
            sunSlotPlusOne <= lighting->lights.size() &&
            lighting->lights[sunSlotPlusOne - 1].cone_enabled[2] != 0.0f;
        quality_->bind_material(gl_, state, normalTechnique, normalTechnique || fixedDirectKnown,
                                lighting ? &*lighting : nullptr, instance.material, overrides,
                                qualitySourceComplete);
        return effectiveRaster;
    }
};

} // namespace
} // namespace c4
