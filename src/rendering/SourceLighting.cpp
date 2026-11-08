#include "SourceLighting.hpp"

#include "assets/World.hpp"
#include "core/render_scene.hpp"
#include "game/CameraProjection.hpp"
#include "physics/NativeNumeric.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace c4::rendering {
namespace {
constexpr double source_color_reciprocal = 0.003921568859368563; // 0x1018bd38
constexpr std::uint32_t unlit = 0x20000, self_lit = 0x80000, no_fog = 0x100000, bright = 0x200000;

std::array<float, 3> shader_axes(physics::Vec3 vector) noexcept {
    return {static_cast<float>(vector.x), static_cast<float>(vector.z), static_cast<float>(vector.y)};
}
physics::Vec3 original_normalized(physics::Vec3 vector) noexcept {
    // 0x10042770/0x100426f0: one binary32 sum, sqrt and reciprocal,
    // followed by three multiply/store operations, not independent divisions.
    const float x = static_cast<float>(vector.x), y = static_cast<float>(vector.y), z = static_cast<float>(vector.z);
    const float sum = static_cast<float>((double(y)*y + double(x)*x) + double(z)*z);
    const float magnitude = static_cast<float>(std::sqrt(double(sum)));
    if (magnitude == 0) return vector;
    const float reciprocal = static_cast<float>(1.0 / double(magnitude));
    return {static_cast<float>(double(x)*reciprocal), static_cast<float>(double(y)*reciprocal),
            static_cast<float>(double(z)*reciprocal)};
}
NativeGameVisualState world_visuals(const assets::WorldScene& world) {
    NativeGameVisualState result;
    // The actual 7.07.6 loader sets SUN_ANGLE from INFO. It only loads INFO
    // SUN_COLOR when the existing source color is zero; the original registered
    // default (128,128,128) is nonzero and C4 never changes it. This is not the
    // later A7.60 ambient_color/map-color policy.
    const auto pan = physics::Var::literal(world.sun_azimuth);
    const auto tilt = physics::Var::literal(world.sun_elevation);
    const auto axis = physics::rotation_axes({physics::number(pan), physics::number(tilt), 0})[0];
    result.sun_direction = shader_axes(original_normalized(axis * -1));
    // Registry 0x101d9cb0 third raw 0x61a8000 survives INFO's first-two-angle writes.
    constexpr float sun_distance = 0x61a8000 / 1024.0f;
    result.sun_position = shader_axes({static_cast<float>(double(axis.x) * sun_distance),
                                      static_cast<float>(double(axis.y) * sun_distance),
                                      static_cast<float>(double(axis.z) * sun_distance)});
    result.sun_position_known = true;
    // 0x10040de0: SUN_LIGHT=100 sourceQ10; f32(100/25500) then RGB*factor.
    const float strength = static_cast<float>(100.0 / 25500.0);
    const float component = static_cast<float>(128.0 * double(strength));
    result.sun_color = {component, component, component, 1};
    result.source_gamma = world.gamma;
    // FOG_COLOR source index is actually zero in the initialized original. The
    // source VIEW fog parameters are retained even when hardware fog is off.
    result.fog_start = 102623.0f / 1024;
    result.fog_end = 1024223.0f / 1024;
    result.fog_enabled = false;
    return result;
}
void source_columns(std::array<float, 16>& matrix) noexcept {
    for (unsigned row = 0; row < 4; ++row) std::swap(matrix[4 + row], matrix[8 + row]);
}
int source_integer(float value) noexcept {
    // Source values here are already Q10; arithmetic right-shift publication
    // differs from truncating toward zero for negative entity ambient.
    return static_cast<int>(std::floor(double(value)));
}
} // namespace

bool source_ambient_direct_capable(const SourceLightingDeviceState& device) noexcept {
    return device.capabilitiesKnown && device.supportedADD && device.supportedMOD2X &&
           device.sourceMaxTextureWidth > 512;
}

std::array<std::uint8_t, 256> source_default_ambient_lookup() noexcept {
    // 1005b670 doubles the registered GAMMA1126 before 1005b5b0 builds
    // ambient BYTE_LUT106eed58; this is not the OTHER device gamma ramp.
    const float gamma = static_cast<float>(2.0 * static_cast<float>(1126.0 / 1024));
    const float exponent = static_cast<float>(1.0 / double(gamma));
    float input = static_cast<float>(0.001);
    std::array<std::uint8_t, 256> result{};
    for (auto& byte : result) {
        const float value = static_cast<float>(std::pow(double(input), double(exponent)));
        byte = static_cast<std::uint8_t>(std::clamp(static_cast<int>(double(value) * 255), 0, 255));
        input = static_cast<float>(double(input) + 1.0 / 255);
    }
    return result;
}

bool source_scene_no_floor_hits(const assets::WorldScene& world,
                                const SourceFloorTraceControls& controls) noexcept {
    if (!controls.shadow_mode_known || !controls.normal_world_load_known ||
        controls.shadow_mode_raw != 0xf000 ||
        !world.source_empty_collision_root_known || !(world.info_flags & 0x04u))
        return false;
    for (const auto& model : world.models) {
        // Model is the decoded MDL4/7 class, not an extension-based guess.
        // An imported mesh replaces geometry only; its existing MDL type remains.
        if (!model.model && !(model.sprite.width > 0 && model.sprite.height > 0))
            return false;
        if (!model.morphed_resource.empty() && !model.morphed_model)
            return false;
        if ((!model.imported_resource.empty() || !model.imported_mesh.vertices.empty() ||
             !model.imported_mesh.indices.empty()) && !model.model && !model.morphed_model)
            return false;
    }
    for (const auto& entity : world.entities) {
        if (entity.model_index == assets::no_world_model) {
            if (!entity.resource.empty()) return false;
        } else if (entity.model_index >= world.models.size()) {
            return false;
        }
    }
    // TRACE_FLOOR off skips the update. If on, any POLY-selected legacy or
    // modern route misses under SHADOW_MODE F000 and either SHADOW branch.
    // Legacy 10058e60 filters MDL20/sprite40 before content/geometry, including
    // PASSABLE; its selected H0 has nodebaseNULL/lastIndex-1 and returns no hit.
    // Modern 1007b290 TYPE20/40 content writes nothing; ce0 rejects their
    // PASSABLE physical path. Normal load clears ROOT520/524; parsed empty
    // sections5/11/17 cannot rebuild them. Thus 1003f0d0 never positively
    // copies TEX_* into ENTITY cache, independently of final TRACE_FLOOR/POLY.
    return true;
}

bool source_scene_no_linked_brightness_writes(const assets::WorldScene& world) noexcept {
    if (!world.source_empty_collision_root_known) return false;
    for (const auto& model : world.models) {
        if (model.model) {
            if (!model.model->source_no_linked_brightness_known) return false;
        } else if (!(model.sprite.width > 0 && model.sprite.height > 0)) {
            return false;
        }
        if (model.morphed_model) {
            if (!model.morphed_model->source_no_linked_brightness_known) return false;
        } else if (!model.morphed_resource.empty()) {
            return false;
        }
        if ((!model.imported_resource.empty() || !model.imported_mesh.vertices.empty() ||
             !model.imported_mesh.indices.empty()) && !model.model && !model.morphed_model)
            return false;
    }
    for (const auto& entity : world.entities) {
        if (entity.model_index == assets::no_world_model) {
            if (!entity.resource.empty()) return false;
        } else if (entity.model_index >= world.models.size()) {
            return false;
        }
    }
    // 100602d0 clears prior resource flags; ordinary 10061070 MDL4/7 load,
    // clone/morph and ent_importmesh preserve RESOURCE1000 clear. The actual
    // setter 1005f940 OR1220 belongs to embedded WMB models, excluded by the
    // parser's section17 fact. 10050570 therefore bypasses 1004f750 entirely.
    return true;
}

void initialize_source_entity_lighting_caches(std::span<SourceEntityLightingCache> caches,
                                              bool scene_no_floor_hits,
                                              bool independent_brightness_preserves_constructor) noexcept {
    for (auto& cache : caches) {
        cache = SourceEntityLightingCache{}; // Actual 1003c2d0 constructor words.
        cache.environment_known = scene_no_floor_hits && independent_brightness_preserves_constructor;
        cache.chroma_known = scene_no_floor_hits;
        // Public color/range have independent actual producers.
    }
}

void publish_source_view_ambient(SourceLightingState& state,
                                std::int32_t effective_ambient, bool known) noexcept {
    state.view_ambient = effective_ambient;
    state.view_ambient_known = known;
}

void publish_source_entity_light_color(SourceEntityLightingCache& cache,
                                      const std::array<std::int32_t, 3>& raw_bgr,
                                      bool known) noexcept {
    cache.source_color_raw_bgr = raw_bgr;
    cache.source_color_known = known;
}

void publish_source_entity_light_range(SourceEntityLightingCache& cache,
                                      std::int32_t raw_range, bool known) noexcept {
    cache.light_range_raw = raw_range;
    cache.light_range_known = known;
}

NativeGameVisualState source_game_visual_state(const assets::WorldScene& world,
                                               physics::Vec3 camera_position,
                                               physics::Vec3 camera_angles) {
    auto result = world_visuals(world);
    result.view_position = shader_axes(camera_position);
    result.view_direction = shader_axes(physics::rotation_axes(camera_angles)[0]);
    result.world_to_view = game::source_view_matrix(camera_position, camera_angles);
    source_columns(result.world_to_view);
    result.view_inputs_known = true;
    return result;
}

NativeGameVisualState source_inspection_visual_state(const assets::WorldScene& world,
                                                     const CameraState& camera) {
    if (!camera.projection_known) throw std::runtime_error("Original inspection lighting requires the actual camera view matrix");
    auto result = world_visuals(world);
    result.view_position = {camera.view_position[0], camera.view_position[2], camera.view_position[1]};
    result.world_to_view = camera.view;
    // The shared explicit inspection camera is RH with negative front Z;
    // source D3D camera depth is positive. Convert the actual matrix, not Euler.
    for (unsigned column = 0; column < 4; ++column) result.world_to_view[column * 4 + 2] *= -1;
    source_columns(result.world_to_view);
    result.view_direction = {-camera.view[2], -camera.view[10], -camera.view[6]};
    result.view_inputs_known = true;
    return result;
}

std::array<float, 16> source_inverse_view_matrix(const NativeGameVisualState& visuals) {
    std::array<float, 16> result;
    if (!physics::source_matrix_inverse(visuals.world_to_view, result))
        throw std::runtime_error("Original source view matrix is singular");
    return result;
}

SourceLightingState source_lighting_state(const assets::WorldScene& world,
                                         const NativeGameVisualState& visuals) noexcept {
    SourceLightingState state;
    state.world_to_view = visuals.world_to_view;
    state.view_position = {visuals.view_position[0], visuals.view_position[1], visuals.view_position[2], 1};
    state.view_known = visuals.view_inputs_known;
    state.source_effects_known = (visuals.source_view_flags_known_mask & 0x1000u) != 0;
    state.source_effects_enabled = (visuals.source_view_flags & 0x1000u) == 0;
    state.fog_color = visuals.fog_color;
    state.fog_params = {visuals.fog_start, visuals.fog_end, 1, 0};
    state.fog_enabled = visuals.fog_enabled;
    state.fog_mode = 3;
    state.range_fog = true;
    state.fog_known = true; // Observed original FOG_COLOR0 and disabled device fog; no WDL overrides.
    auto& sun = state.lights[0];
    // 0x1003f7a0 clears the D3DLIGHT record: light alpha is zero, unlike
    // the source FX vecSunColor.w (one) and D3DMATERIAL9 color alphas (one).
    sun.diffuse = {visuals.sun_color[0], visuals.sun_color[1], visuals.sun_color[2], 0};
    sun.specular = sun.diffuse;
    state.sun_position_known = visuals.sun_position_known;
    if (state.sun_position_known) {
        sun.position_range[0] = visuals.sun_position[0];
        sun.position_range[1] = visuals.sun_position[1];
        sun.position_range[2] = visuals.sun_position[2];
    }
    sun.direction_type = {visuals.sun_direction[0], visuals.sun_direction[1], visuals.sun_direction[2], 3};
    sun.attenuation_falloff = {1, 0, 0, 0};
    sun.cone_enabled = {0, 0, 1, 0};
    state.light_count = 1;
    state.sun_slot_plus_one = 1; // 0x1003f7a0 SUN constructor; 0x10040520 fixed slot zero.
    state.lights_known = true;
    // There are no original WMB LIGHT objects or WDL light emitters. The
    // remaining frame slots stay disabled; no nearest-light approximation.
    (void)world;
    return state;
}

SourceLightingUniforms source_lighting_uniforms(const SourceLightingState& state,
                                                const NativeMaterialState& material,
                                                std::uint32_t entity_flags,
                                                const OriginalModelAlphaDecision* alpha,
                                                const SourceEntityLightingCache* entity_cache,
                                                const OriginalMaterialDrawState* draw_state,
                                                const SourceLightingDeviceState* device) noexcept {
    SourceLightingUniforms result;
    result.lights = state.lights;
    result.world_to_view = state.world_to_view;
    result.view_position = state.view_position;
    result.fog_color = state.fog_color;
    result.fog_params = state.fog_params;
    result.diffuse = material.diffuse;
    result.ambient = material.ambient;
    result.specular = material.specular;
    result.emissive = material.emissive;
    result.material_params = {material.power, 2, 0, 0};
    result.controls = {1, 1, 1, 0}; // Actual 0x10043d90 reset state, not GL defaults.
    // Draw-time 0x10044d10 depends on the previously bound material, not just
    // the selected material/entity flags. Only its typed result proves alpha.
    if (alpha) result.diffuse[3] = alpha->diffuse_alpha;
    static constexpr SourceEntityLightingCache unknown_cache;
    const auto& cache = entity_cache ? *entity_cache : unknown_cache;
    const bool alternate = (entity_flags & 0x08000000u) != 0;
    const int environment = cache.environment;
    int red, green, blue;
    if (alternate) {
        // 10054220 publishes public Q10 RGB to 1003f3f0's scratch integers;
        // 10044d10 bypasses normal floor/chroma/VIEW ambient conversion.
        red = cache.source_color_raw_bgr[2] >> 10;
        green = cache.source_color_raw_bgr[1] >> 10;
        blue = cache.source_color_raw_bgr[0] >> 10;
    } else {
        const auto entity_ambient_integer = static_cast<std::int16_t>(source_integer(material.entity_ambient_percent));
        const int entity_ambient = std::clamp(int(entity_ambient_integer) +
                                             state.view_ambient, -127, 127);
        int scalar = entity_ambient;
        if (!(entity_flags & unlit)) scalar += environment;
        else if (entity_flags & bright) scalar += 127;
        red = scalar + cache.chroma_bgr[2];
        green = scalar + cache.chroma_bgr[1];
        blue = scalar + cache.chroma_bgr[0];
        if ((entity_flags & self_lit) && cache.light_range_raw < 1) {
            red += cache.source_color_raw_bgr[2] >> 10;
            green += cache.source_color_raw_bgr[1] >> 10;
            blue += cache.source_color_raw_bgr[0] >> 10;
        }
        const int maximum = std::max({red, green, blue});
        if (maximum > 255) {
            const float factor = static_cast<float>(255.0 / maximum);
            red = static_cast<int>(double(red) * factor);
            green = static_cast<int>(double(green) * factor);
            blue = static_cast<int>(double(blue) * factor);
        }
    }
    bool ambient_encoding_known = device && device->capabilitiesKnown;
    if (ambient_encoding_known && !source_ambient_direct_capable(*device)) {
        // The source indexes whole scratch integers without bounds checks.
        // Out-of-range source memory reads are unproved, not clamp/wrap rules.
        ambient_encoding_known = device->sourceLUTKnown && red >= 0 && red <= 255 &&
                                 green >= 0 && green <= 255 && blue >= 0 && blue <= 255;
        if (ambient_encoding_known) {
            red = device->sourceLUT[red];
            green = device->sourceLUT[green];
            blue = device->sourceLUT[blue];
        }
    }
    // 0x1003f3f0 shifts/ORs whole source integers before byte extraction.
    // Negative chroma can therefore affect higher channels; independently
    // masking three inputs would not reproduce the original packed DWORD.
    const std::uint32_t packed = (std::uint32_t(red) << 16) |
                                 (std::uint32_t(green) << 8) | std::uint32_t(blue);
    red = int((packed >> 16) & 255);
    green = int((packed >> 8) & 255);
    blue = int(packed & 255);
    // Every authored Terrain4/5 pass overrides RS_AMBIENT with vecLight*10.
    // vecLight is this pre-effect packed RGB /255. Original D3DX9_30 clamps
    // float channels and rounds value*255+0.5; over the byte input domain this
    // is exactly min(byte*10,255), including the binary32 intermediate.
    const auto effective_technique = draw_state ? draw_state->technique : material.technique;
    if (effective_technique >= MaterialTechnique::Terrain4A &&
        effective_technique <= MaterialTechnique::Terrain5C) {
        red = std::min((red & 255) * 10, 255);
        green = std::min((green & 255) * 10, 255);
        blue = std::min((blue & 255) * 10, 255);
    }
    result.global_ambient = {static_cast<float>(double(red & 255) / 255),
                             static_cast<float>(double(green & 255) / 255),
                             static_cast<float>(double(blue & 255) / 255), 0};
    // 0x10040520 non-bank sun scale, with source integer publication before
    // floating arithmetic. Entity shader fAlbedo is a separate source value.
    const auto material_raw = static_cast<std::int32_t>(double(material.material_albedo_percent) * 1024);
    const auto doubled = static_cast<std::int64_t>(material_raw) * 2;
    const int material_integer = static_cast<int>(std::floor(double(doubled) / 1024));
    const float scale = std::clamp(static_cast<float>(double(material_integer + environment - 100) / 127), 0.0f, 1.0f);
    for (unsigned channel = 0; channel < 3; ++channel) {
        result.lights[0].diffuse[channel] = static_cast<float>(double(result.lights[0].diffuse[channel]) * scale);
        result.lights[0].specular[channel] = result.lights[0].diffuse[channel];
    }
    result.lights[0].cone_enabled[2] = material_raw > 0 ? 1.0f : 0.0f;
    const bool material_known = material.color_known_mask == 0xfff && material.power_known && material.material_albedo_known;
    const auto has_rgb = [](const auto& color) { return color[0] != 0 || color[1] != 0 || color[2] != 0; };
    // Zero material coefficients make the unknown floor cache irrelevant;
    // mat_unlit is genuinely emissive, not an invented white lighting fallback.
    const bool ambient_required = has_rgb(material.ambient);
    const bool environment_required = (ambient_required && !alternate && !(entity_flags & unlit)) ||
        (material_raw > 0 && (has_rgb(material.diffuse) || has_rgb(material.specular)));
    const bool ambient_known = ambient_encoding_known && (alternate ? cache.source_color_known :
        (cache.chroma_known && state.view_ambient_known &&
         (!(entity_flags & self_lit) || (cache.light_range_known &&
          (cache.light_range_raw >= 1 || cache.source_color_known)))));
    const bool cache_known = (!ambient_required || ambient_known) &&
                             (!environment_required || cache.environment_known);
    result.status = {std::uint32_t(state.lights_known && cache_known &&
                                    state.fog_known && state.view_known && material_known),
                     state.light_count, std::uint32_t(state.fog_enabled && !(entity_flags & (no_fog | 0x08000000u))), state.fog_mode};
    result.options = {std::uint32_t(state.range_fog), std::uint32_t(alpha != nullptr),
                      alpha ? std::uint32_t(alpha->modulate_diffuse ? 4 : 2) : 0, state.sun_slot_plus_one};
    return result;
}

SourceLightingUniforms source_view_entity_uniforms(const NativeMaterialState& material,
                                                   std::uint32_t entity_flags,
                                                   const OriginalModelAlphaDecision* alpha) noexcept {
    SourceLightingState state;
    state.world_to_view = game::source_view_matrix({}, {});
    for (unsigned row = 0; row < 4; ++row)
        std::swap(state.world_to_view[4 + row], state.world_to_view[8 + row]);
    state.view_known = true;
    state.source_effects_enabled = true; // Private cam_ibre VIEW flags are zero.
    state.source_effects_known = true;
    state.fog_known = true; // Actual source FOG_COLOR index zero disables fog.
    state.fog_mode = 3;
    state.range_fog = true;
    // The fixed SUN slot is disabled for the actual HUD's material albedo zero.
    // Do not claim an arbitrary source MATERIAL's sun/VIEW environment known.
    state.lights_known = material.material_albedo_known && material.material_albedo_percent == 0;
    return source_lighting_uniforms(state, material, entity_flags, alpha);
}

} // namespace c4::rendering
