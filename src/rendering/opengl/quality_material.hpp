#pragma once

#include "core/render_scene.hpp"
#include "rendering/Material.hpp"
#include "rendering/SourceLighting.hpp"
#include "rendering/opengl/gl.hpp"
#include "rendering/quality/OpenGLQuality.hpp"
#include "rendering/quality/ModernMaterial.hpp"
#include "rendering/quality/ShadowCascades.hpp"
#include "QualityShaders.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

namespace c4 {
namespace {

using QualityMatrix = rendering::quality::OpenGLQuality::Matrix;

// The source shader world is (x,z,y); native mesh/model/world positions are Z-up.
// Only INPUT columns change: the output remains the same source view/light clip.
inline QualityMatrix quality_source_matrix_to_native(QualityMatrix source) {
    for (unsigned row = 0; row < 4; ++row) std::swap(source[4 + row], source[8 + row]);
    return source;
}
inline rendering::quality::ShadowCascades source_shadow_cascades_to_native(
    rendering::quality::ShadowCascades source) {
    for (unsigned i = 0; i < std::min(source.count, 4u); ++i)
        source.matrices[i] = quality_source_matrix_to_native(source.matrices[i]);
    return source;
}

inline QualityMatrix quality_matrix_multiply(const QualityMatrix& a, const QualityMatrix& b) {
    QualityMatrix result{};
    for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned k = 0; k < 4; ++k)
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return result;
}
inline std::optional<QualityMatrix> quality_matrix_inverse(const QualityMatrix& matrix) {
    double rows[4][8]{};
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            const float value = matrix[column * 4 + row];
            if (!std::isfinite(value)) return std::nullopt;
            rows[row][column] = value;
        }
        rows[row][row + 4] = 1.0;
    }
    for (unsigned column = 0; column < 4; ++column) {
        unsigned pivot = column;
        for (unsigned row = column + 1; row < 4; ++row)
            if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) pivot = row;
        if (!std::isfinite(rows[pivot][column]) || rows[pivot][column] == 0.0) return std::nullopt;
        if (pivot != column)
            for (unsigned k = 0; k < 8; ++k) std::swap(rows[pivot][k], rows[column][k]);
        const double divisor = rows[column][column];
        for (unsigned k = 0; k < 8; ++k) rows[column][k] /= divisor;
        for (unsigned row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = rows[row][column];
            for (unsigned k = 0; k < 8; ++k) rows[row][k] -= factor * rows[column][k];
        }
    }
    QualityMatrix result{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column) {
            result[column * 4 + row] = static_cast<float>(rows[row][column + 4]);
            if (!std::isfinite(result[column * 4 + row])) return std::nullopt;
        }
    return result;
}

struct QualityCameraFrame {
    QualityMatrix projection{}, inverseProjection{}, inverseViewProjection{};
    std::array<float, 4> positiveDepthRow{};
    float nearDepth{}, farDepth{}, clipNear{}, clipFar{};
};
// Prefer the actual RH camera projection/clipping metadata. The fallback uses
// the source D3D positive-Z view, converted to the SSAO negative-Z convention.
// Infinite/invalid volumes cannot fit cascades and are explicitly rejected.
inline std::optional<QualityCameraFrame> quality_camera_frame(const rendering::FrameSnapshot& frame) {
    if (frame.camera.projection_known) {
        const auto& camera = frame.camera;
        if (!std::isfinite(camera.clip_near) || !std::isfinite(camera.clip_far)
            || camera.clip_near <= 0.0f || camera.clip_far <= camera.clip_near) return std::nullopt;
        for (unsigned i = 0; i < 16; ++i)
            if (!std::isfinite(camera.view[i]) || !std::isfinite(camera.projection[i])
                || !std::isfinite(camera.inverse_projection[i])) return std::nullopt;
        const auto inverseVP = quality_matrix_inverse(camera.view_projection);
        if (!inverseVP) return std::nullopt;
        QualityCameraFrame result;
        result.projection = camera.projection;
        result.inverseProjection = camera.inverse_projection;
        result.inverseViewProjection = *inverseVP;
        result.positiveDepthRow = {-camera.view[2], -camera.view[6], -camera.view[10], -camera.view[14]};
        result.nearDepth = camera.clip_near;
        result.farDepth = camera.clip_far;
        // Only classify the documented GL clip endpoints; never recover the
        // actual far distance by inverting a quantized perspective depth.
        result.clipNear = camera.projection[10] > 0.0f ? 1.0f : -1.0f;
        result.clipFar = -result.clipNear;
        return result;
    }
    if (!frame.visuals || !frame.visuals->view_inputs_known) return std::nullopt;
    QualityCameraFrame result;
    const auto nativeView = quality_source_matrix_to_native(frame.visuals->world_to_view);
    result.positiveDepthRow = {nativeView[2], nativeView[6], nativeView[10], nativeView[14]};
    const auto inverseView = quality_matrix_inverse(nativeView);
    const auto inverseVP = quality_matrix_inverse(frame.camera.view_projection);
    if (!inverseView || !inverseVP) return std::nullopt;
    result.projection = quality_matrix_multiply(frame.camera.view_projection, *inverseView);
    const auto inverseProjection = quality_matrix_inverse(result.projection);
    if (!inverseProjection) return std::nullopt;
    result.inverseProjection = *inverseProjection;
    result.inverseViewProjection = *inverseVP;
    const auto depth = [&](float clip) -> std::optional<float> {
        const double z = static_cast<double>((*inverseProjection)[10]) * clip + (*inverseProjection)[14];
        const double w = static_cast<double>((*inverseProjection)[11]) * clip + (*inverseProjection)[15];
        if (w == 0.0) return std::nullopt;
        const float distance = static_cast<float>(z / w);
        if (!std::isfinite(distance) || distance <= 0.0f) return std::nullopt;
        return distance;
    };
    const auto minus = depth(-1.0f), plus = depth(1.0f);
    if (!minus || !plus || *minus == *plus) return std::nullopt;
    result.nearDepth = std::min(*minus, *plus);
    result.farDepth = std::max(*minus, *plus);
    result.clipNear = *minus < *plus ? -1.0f : 1.0f;
    result.clipFar = -result.clipNear;
    // P_RH = P_LH * flipZ; inverse(P_RH) = flipZ * inverse(P_LH).
    for (unsigned row = 0; row < 4; ++row) result.projection[8 + row] *= -1.0f;
    for (unsigned column = 0; column < 4; ++column) result.inverseProjection[column * 4 + 2] *= -1.0f;
    return result;
}

inline std::string quality_material_glsl() {
    static constexpr char adapter[] = R"C4QM(
uniform bool qmEnhanced, qmHdr, qmPbr, qmNormal, qmDirectKnown, qmViewKnown;
uniform bool qmNormalAlphaMask;
uniform vec3 qmViewPosition, qmSunDirection;
uniform float qmMetallic, qmRoughness, qmReflectivity;
uniform sampler2D qmShadowAtlas;
uniform int qmCascadeCount;
uniform mat4 qmLight0, qmLight1, qmLight2, qmLight3;
uniform vec4 qmTile0, qmTile1, qmTile2, qmTile3, qmSplits, qmViewDepthRow;
uniform vec2 qmAtlasTexel, qmShadowBias0, qmShadowBias1, qmShadowBias2, qmShadowBias3;

float c4_material_visibility(vec3 world, float normalDotLight) {
    float depth = dot(qmViewDepthRow, vec4(world, 1.0));
    int cascade = c4_quality_shadow_cascade(depth, qmSplits, qmCascadeCount);
    vec4 position = vec4(world, 1.0);
    if (cascade == 0) return c4_quality_shadow_pcf(qmShadowAtlas, qmLight0 * position,
        qmTile0, qmAtlasTexel, vec2(0.5, 0.5), normalDotLight, qmShadowBias0.x, qmShadowBias0.y);
    if (cascade == 1) return c4_quality_shadow_pcf(qmShadowAtlas, qmLight1 * position,
        qmTile1, qmAtlasTexel, vec2(0.5, 0.5), normalDotLight, qmShadowBias1.x, qmShadowBias1.y);
    if (cascade == 2) return c4_quality_shadow_pcf(qmShadowAtlas, qmLight2 * position,
        qmTile2, qmAtlasTexel, vec2(0.5, 0.5), normalDotLight, qmShadowBias2.x, qmShadowBias2.y);
    if (cascade == 3) return c4_quality_shadow_pcf(qmShadowAtlas, qmLight3 * position,
        qmTile3, qmAtlasTexel, vec2(0.5, 0.5), normalDotLight, qmShadowBias3.x, qmShadowBias3.y);
    return 1.0;
}
float c4_material_linear_to_srgb_channel(float value) {
    value = max(value, 0.0);
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}
vec3 c4_material_linear_to_srgb(vec3 value) {
    return vec3(c4_material_linear_to_srgb_channel(value.r),
                c4_material_linear_to_srgb_channel(value.g),
                c4_material_linear_to_srgb_channel(value.b));
}
// sourceDirectSrgb is the ORIGINAL full result minus the SUN-excluded result,
// including actual lightmap/cube factors. SUN ambient and ALL other lights stay
// in the residual. An unknown decomposition bypasses BOTH lighting changes.
vec4 c4_apply_quality(vec4 result, vec4 normalSample,
                      vec3 nativeWorldPosition, vec3 nativeWorldNormal,
                      vec3 tangentView, vec3 tangentSun, vec3 sourceDirectSrgb,
                      vec3 nativeResidualSrgb) {
    if (!qmEnhanced) return result;
    vec3 normal = qmNormal ? normalSample.rgb * 2.0 - vec3(1.0) : nativeWorldNormal;
    vec3 view = qmNormal ? tangentView : qmViewPosition - nativeWorldPosition;
    vec3 light = qmNormal ? tangentSun : -qmSunDirection;
    float nl = max(dot(c4_pbr_unit(normal), c4_pbr_unit(light)), 0.0);
    float visibility = qmDirectKnown && qmViewKnown
        ? c4_material_visibility(nativeWorldPosition, nl) : 1.0;
    if (qmPbr && qmDirectKnown && qmViewKnown) {
        float mask = qmNormalAlphaMask ? normalSample.a : 1.0;
        // Preserve authored ambient/emissive/lightmap/non-SUN terms. The caller
        // may change only the existing cube RGB mix; no fabricated IBL is added.
        vec3 indirect = c4_quality_srgb_to_linear(max(nativeResidualSrgb, vec3(0.0)));
        vec3 direct = c4_quality_source_sun_metallic(
            sourceDirectSrgb, result.rgb - sourceDirectSrgb,
            qmMetallic, qmRoughness, mask, normal, view, light);
        result.rgb = indirect + direct * visibility;
        if (!qmHdr) result.rgb = clamp(c4_material_linear_to_srgb(result.rgb), 0.0, 1.0);
    } else {
        if (qmDirectKnown) result.rgb += sourceDirectSrgb * (visibility - 1.0);
        if (qmHdr) result.rgb = c4_quality_srgb_to_linear(max(result.rgb, vec3(0.0)));
    }
    return result; // Source alpha/OVERLAY comparison stays in the caller.
}
)C4QM";
    std::string source;
    source.reserve(sizeof(rendering::quality::pbr_shader) + sizeof(rendering::quality::shadow_shader)
                   + sizeof(adapter) - 3);
    source.append(rendering::quality::pbr_shader, sizeof(rendering::quality::pbr_shader) - 1);
    source.append(rendering::quality::shadow_shader, sizeof(rendering::quality::shadow_shader) - 1);
    source.append(adapter, sizeof(adapter) - 1);
    return source;
}

struct QualityMaterialUniforms {
    GLint enhanced, hdr, pbr, normal, direct_known, view_known, alpha_mask;
    GLint metallic, roughness, reflectivity, view_position, sun_direction;
    GLint shadow_atlas, cascade_count, splits, view_depth_row, atlas_texel;
    std::array<GLint, 4> light{}, tile{}, shadow_bias{};
    std::array<float, 3> frame_sun_color{}, frame_sun_direction{};
    bool pbr_enabled{};
    bool source_light_known{};
    bool pbr_active{};

    QualityMaterialUniforms(InspectionGL& gl, GLuint program) {
        const auto location = [&](const char* name) { return gl.glGetUniformLocation(program, name); };
        enhanced = location("qmEnhanced"); hdr = location("qmHdr"); pbr = location("qmPbr");
        normal = location("qmNormal"); direct_known = location("qmDirectKnown");
        view_known = location("qmViewKnown"); alpha_mask = location("qmNormalAlphaMask");
        metallic = location("qmMetallic"); roughness = location("qmRoughness");
        reflectivity = location("qmReflectivity");
        view_position = location("qmViewPosition"); sun_direction = location("qmSunDirection");
        shadow_atlas = location("qmShadowAtlas"); cascade_count = location("qmCascadeCount");
        splits = location("qmSplits"); view_depth_row = location("qmViewDepthRow");
        atlas_texel = location("qmAtlasTexel");
        constexpr const char* lights[]{"qmLight0", "qmLight1", "qmLight2", "qmLight3"};
        constexpr const char* tiles[]{"qmTile0", "qmTile1", "qmTile2", "qmTile3"};
        constexpr const char* biases[]{"qmShadowBias0", "qmShadowBias1", "qmShadowBias2", "qmShadowBias3"};
        for (unsigned i = 0; i < 4; ++i) {
            light[i] = location(lights[i]); tile[i] = location(tiles[i]);
            shadow_bias[i] = location(biases[i]);
        }
    }
    // Cascades here and in draw_shadows MUST use the same NATIVE world basis.
    void bind_frame(InspectionGL& gl, const rendering::quality::OpenGLQuality* helper,
                    const rendering::quality::ShadowCascades* cascades,
                    const rendering::NativeGameVisualState& visuals,
                    const std::array<float, 4>* nativePositiveDepthRow = nullptr) {
        const auto* settings = helper ? &helper->resolved() : nullptr;
        const bool active = settings && settings->enhanced();
        pbr_enabled = active && settings->pbr;
        pbr_active = false;
        gl.glUniform1i(enhanced, active); gl.glUniform1i(hdr, active && settings->internal_hdr);
        gl.glUniform1i(view_known, visuals.view_inputs_known);
        const std::array<float, 3> eye{visuals.view_position[0], visuals.view_position[2], visuals.view_position[1]};
        const std::array<float, 3> sun{visuals.sun_direction[0], visuals.sun_direction[2], visuals.sun_direction[1]};
        frame_sun_direction = sun;
        for (unsigned i = 0; i < 3; ++i) frame_sun_color[i] = visuals.sun_color[i];
        source_light_known = visuals.view_inputs_known;
        double directionLength = 0.0;
        for (unsigned i = 0; i < 3; ++i) {
            source_light_known = source_light_known && std::isfinite(sun[i]) && std::isfinite(eye[i])
                && std::isfinite(visuals.sun_color[i]) && visuals.sun_color[i] >= 0.0f;
            directionLength += static_cast<double>(sun[i]) * sun[i];
        }
        source_light_known = source_light_known && directionLength > 0.0;
        gl.glUniform3fv(view_position, 1, eye.data()); gl.glUniform3fv(sun_direction, 1, sun.data());
        const auto view = quality_source_matrix_to_native(visuals.world_to_view);
        const std::array<float, 4> depthRow{view[2], view[6], view[10], view[14]};
        gl.glUniform4fv(view_depth_row, 1, nativePositiveDepthRow ? nativePositiveDepthRow->data() : depthRow.data());
        unsigned count = 0;
        if (active && visuals.view_inputs_known && cascades && helper->shadow_texture()
            && helper->capabilities().max_shadow_size
            && settings->shadows != rendering::quality::ShadowQuality::Off)
            count = std::min({cascades->count, settings->shadow_cascades, 4u});
        gl.glUniform1i(cascade_count, static_cast<GLint>(count));
        if (!count) return;
        gl.glUniform4fv(splits, 1, cascades->split_depths.data());
        const float texel = 1.0f / (2.0f * static_cast<float>(helper->capabilities().max_shadow_size));
        const std::array<float, 2> texels{texel, texel};
        gl.glUniform2fv(atlas_texel, 1, texels.data());
        for (unsigned i = 0; i < count; ++i) {
            gl.glUniformMatrix4fv(light[i], 1, GL_FALSE, cascades->matrices[i].data());
            gl.glUniform2fv(shadow_bias[i], 1, cascades->depth_bias[i].data());
            const auto atlasTile = helper->shadow_tile(i);
            gl.glUniform4fv(tile[i], 1, atlasTile.data());
        }
        GLint previousActive{}; glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActive);
        // Source material samplers occupy units 0..5, including water/lightmap.
        gl.glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, helper->shadow_texture());
        gl.glUniform1i(shadow_atlas, 6); gl.glActiveTexture(static_cast<GLenum>(previousActive));
    }
    void bind_material(InspectionGL& gl, const rendering::NativeMaterialState& material,
                       bool normalTechnique, bool sourceDirectKnown,
                       const rendering::SourceLightingUniforms* selectedFixed = nullptr,
                       std::string_view material_name = {},
                       std::span<const rendering::quality::ModernMaterialOverride> overrides = {},
                       bool sourceComplete = true) {
        constexpr unsigned specularRgb = 7u << 6;
        const bool fixed = !normalTechnique && selectedFixed;
        const auto& sourceSpecular = fixed ? selectedFixed->specular : material.specular;
        const float sourcePower = fixed ? selectedFixed->material_params[0] : material.power;
        static constexpr std::array<float, 3> neutralTint{1.0f, 1.0f, 1.0f};
        const float* sourceTint = fixed ? selectedFixed->diffuse.data() : neutralTint.data();
        const auto modern = pbr_enabled ? [&] {
            if (!fixed) return rendering::quality::modern_material_data(material, material_name, overrides);
            // Project only the selected draw coefficients; the named MATERIAL
            // may differ from the actual MDL palette/factory material.
            rendering::NativeMaterialState selected;
            selected.technique = rendering::material_requires_tangent(material.technique)
                ? rendering::MaterialTechnique::FixedFunction : material.technique;
            selected.specular = sourceSpecular;
            selected.power = sourcePower;
            selected.power_known = selectedFixed->status[0] != 0;
            selected.color_known_mask = selected.power_known ? specularRgb : 0;
            selected.texture_factor = material.texture_factor;
            selected.texture_factor_known = material.texture_factor_known;
            selected.skills[8] = material.skills[8];
            selected.skills_known_mask = material.skills_known_mask & (1u << 8);
            return rendering::quality::modern_material_data(selected, material_name, overrides);
        }() : std::array<float, 4>{0.0f, 1.0f, 0.0f, 0.0f};
        const rendering::SourceLight* selectedSun = nullptr;
        if (fixed) {
            const unsigned slotPlusOne = selectedFixed->options[3];
            if (slotPlusOne && slotPlusOne <= std::min(selectedFixed->status[1], 8u))
                selectedSun = &selectedFixed->lights[slotPlusOne - 1];
        }
        std::array<float, 3> selectedSunDirection{};
        const float* sourceSunColor = frame_sun_color.data();
        const float* sourceSunDirection = frame_sun_direction.data();
        if (selectedSun) {
            selectedSunDirection = {selectedSun->direction_type[0], selectedSun->direction_type[2],
                                    selectedSun->direction_type[1]};
            sourceSunColor = selectedSun->diffuse.data();
            sourceSunDirection = selectedSunDirection.data();
        }
        // SUN identity is authored provenance, NOT a directional-type heuristic.
        // The caller subtracts its SUN-excluded result, preserving other lights.
        const bool direct = sourceDirectKnown && (!fixed || (selectedFixed->status[0]
            && selectedFixed->controls[0] && selectedSun && selectedSun->cone_enabled[2] > 0.5f));
        // Fixed draws use the ACTUAL selected D3D palette/factory payload,
        // never coefficients from a differently selected named MATERIAL.
        bool known = fixed ? selectedFixed->status[0] != 0
            : material.power_known && (material.color_known_mask & specularRgb) == specularRgb;
        known = known && std::isfinite(sourcePower) && sourcePower >= 0.0f;
        for (unsigned i = 0; i < 3; ++i)
            known = known && std::isfinite(sourceSpecular[i]);
        bool lightKnown = true;
        double sunLength = 0.0;
        for (unsigned i = 0; i < 3; ++i) {
            lightKnown = lightKnown && std::isfinite(sourceTint[i])
                && std::isfinite(sourceSunColor[i]) && sourceSunColor[i] >= 0.0f
                && std::isfinite(sourceSunDirection[i]);
            sunLength += static_cast<double>(sourceSunDirection[i]) * sourceSunDirection[i];
        }
        lightKnown = lightKnown && sunLength > 0.0;
        pbr_active = pbr_enabled && sourceComplete && source_light_known && lightKnown && direct
            && (known || modern[3] == 2.0f);
        gl.glUniform1i(pbr, pbr_active);
        gl.glUniform1i(normal, normalTechnique); gl.glUniform1i(direct_known, direct);
        using T = rendering::MaterialTechnique;
        gl.glUniform1i(alpha_mask, normalTechnique
            && (material.technique == T::NormalTileSpec || material.technique == T::NormalTSpecShadow));
        gl.glUniform1f(metallic, modern[0]); gl.glUniform1f(roughness, modern[1]);
        gl.glUniform1f(reflectivity, modern[2]);
        gl.glUniform3fv(sun_direction, 1, sourceSunDirection);
    }
};

} // namespace
} // namespace c4
