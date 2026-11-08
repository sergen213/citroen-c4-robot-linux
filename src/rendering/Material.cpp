#include "Material.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace c4::rendering {
namespace {
using T = MaterialTechnique;
struct MaterialConstants {
    std::string_view name;
    std::array<float, 20> skills;
    std::uint32_t skills_mask;
    std::array<float, 3> ambient, diffuse, specular, emissive;
    std::uint16_t color_mask;
    float power, albedo;
    bool power_known, albedo_known;
    std::uint32_t texture_factor;
    bool texture_factor_known;
    float reflection_percent;
    bool reflection_percent_known;
    bool definition_known{true};
};
struct BuiltinMaterialConstants {
    std::string_view name;
    std::array<float, 4> colors; // Ambient, diffuse, specular, emissive.
    float power, albedo;
};
constexpr float native_color(unsigned value) noexcept {
    return static_cast<float>(static_cast<double>(value) * 0.003921568859368563);
}
// Engine constructor 0x1001acb0 -> 0x1001ac50, independent of WDL profile buckets.
constexpr std::array<BuiltinMaterialConstants, 5> builtin_material_constants{{
    {"mat_model",   {native_color(200), native_color(200), 0, 0},                 0,  50},
    {"mat_sprite",  {native_color(200), native_color(128), 0, 0},                 0,   0},
    {"mat_metal",   {native_color(100), native_color(100), native_color(255), 0}, 10, 100},
    {"mat_terrain", {native_color(200), native_color(200), 0, 0},                 0,  50},
    {"mat_unlit",   {0, 0, 0, native_color(128)},                                 0,   0}
}};
#include "MaterialConstantsVariants.inc"
constexpr std::array effect_names{
    std::pair{"Normal_Optim.fx", T::NormalOptim},
    std::pair{"Normal_TileOptim.fx", T::NormalTileOptim},
    std::pair{"Normal_TileSpec.fx", T::NormalTileSpec},
    std::pair{"Normal_Mix.fx", T::NormalMix},
    std::pair{"Normal_T_Opt_Golge.fx", T::NormalTOptShadow},
    std::pair{"Normal_T_Spc_Golge.fx", T::NormalTSpecShadow},
    std::pair{"CubeNormal_Optim.fx", T::CubeNormalOptim},
    std::pair{"CubeNormal_Car.fx", T::CubeNormalCar},
    std::pair{"CubeNormal_Mix.fx", T::CubeNormalMix},
    std::pair{"EnvCube_Mix.fx", T::EnvCubeMix},
    std::pair{"EnvCube_Alpha.fx", T::EnvCubeAlpha},
    std::pair{"Shadow_Map.fx", T::ShadowMap},
    std::pair{"multi_texture4a.fx", T::Terrain4A},
    std::pair{"multi_texture4b.fx", T::Terrain4B},
    std::pair{"multi_texture4c.fx", T::Terrain4C},
    std::pair{"multi_texture5a.fx", T::Terrain5A},
    std::pair{"multi_texture5b.fx", T::Terrain5B},
    std::pair{"multi_texture5c.fx", T::Terrain5C},
    std::pair{"water_entskin.fx", T::Water}
};
constexpr char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
bool equal_filename(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
    return true;
}
const BuiltinMaterialConstants* builtin_material(std::string_view name) noexcept {
    for (const auto& constants : builtin_material_constants)
        if (equal_filename(name, constants.name)) return &constants;
    return nullptr;
}
void apply_builtin_constants(NativeMaterialState& state, const BuiltinMaterialConstants& constants,
                             bool preserve_palette) noexcept {
    state.material_albedo_percent = constants.albedo;
    state.material_albedo_known = true;
    if (preserve_palette) return;
    const std::array destinations{&state.ambient, &state.diffuse, &state.specular, &state.emissive};
    for (unsigned i = 0; i < destinations.size(); ++i) {
        const auto value = constants.colors[i];
        *destinations[i] = {value, value, value, 1};
    }
    state.ambient_alpha_known = true;
    state.color_known_mask = 0xfff;
    state.power = constants.power;
    state.power_known = true;
}
bool terrain(T t) noexcept { return t >= T::Terrain4A && t <= T::Terrain5C; }
bool cube(T t) noexcept { return t >= T::CubeNormalOptim && t <= T::EnvCubeAlpha; }
SamplerState address(AddressMode mode) noexcept { return {mode, mode, AddressMode::Unspecified, false, false}; }
} // namespace

OriginalModelAlphaDecision original_model_alpha_decision(const OriginalModelAlphaInput& input) noexcept {
    constexpr float threshold = 0.9990000128746033f;
    OriginalModelAlphaDecision result{true, true, true, BlendFactor::SourceAlpha,
                                     BlendFactor::InverseSourceAlpha, input.view_alpha};
    if (input.entity_transparent) {
        // Source promotes the float reciprocal to double, then rounds the product before min.
        const float entity_alpha = static_cast<float>(
            static_cast<double>(input.entity_alpha_percent) * 0.009999999776482582);
        result.diffuse_alpha = std::min(entity_alpha, input.view_alpha);
    }
    if (input.entity_transparent || input.texture_source_alpha_flag ||
        (input.prior_material_present && input.prior_ambient_alpha < threshold)) {
        // 0x10044430: material transparency, with the original BRIGHT/global additive gate.
        result.depth_write = false;
        if (input.entity_bright_0x200000 && input.source_additive_enabled)
            result.destination_blend = BlendFactor::One;
    } else if (input.view_alpha >= threshold) {
        result.modulate_diffuse = false;
        result.alpha_blend = false;
        result.diffuse_alpha = 1.0f;
    }
    if (input.preserve_view_material_context)
        result.diffuse_alpha = input.prior_diffuse_alpha;
    else if (input.prior_material_present)
        result.diffuse_alpha = std::min(result.diffuse_alpha, input.prior_ambient_alpha);
    return result;
}

RasterState apply_original_model_alpha(const RasterState& raster,
                                      const OriginalModelAlphaDecision& alpha) noexcept {
    auto result = raster;
    if ((raster.effect_override_mask & BlendEnable) == 0) result.blend = alpha.alpha_blend;
    if ((raster.effect_override_mask & DepthTest) == 0) result.depth_test = true;
    if ((raster.effect_override_mask & DepthWrite) == 0) result.depth_write = alpha.depth_write;
    if ((raster.effect_override_mask & SourceBlend) == 0) result.source_blend = alpha.source_blend;
    if ((raster.effect_override_mask & DestinationBlend) == 0)
        result.destination_blend = alpha.destination_blend;
    return result;
}

void configure_source_bitmap_raster(NativeMaterialState& state, bool overlay_requested,
                                    bool source_alpha_flag) noexcept {
    // 0x10044460: effective ENTITY.OVERLAY tests alpha only without bitmap flag 0x100.
    const auto comparison = overlay_requested && !source_alpha_flag
        ? AlphaComparison::GreaterEqual : AlphaComparison::Always;
    state.source_pass.raster.alpha_comparison = comparison;
    state.source_pass.raster.alpha_reference = 100.0f / 255.0f;
    const auto inherit = [comparison](RasterState& raster) {
        if ((raster.effect_override_mask & AlphaTestEnable) != 0) return;
        raster.alpha_comparison = comparison;
        raster.alpha_reference = 100.0f / 255.0f;
    };
    inherit(state.raster);
    for (auto& pass : state.passes) inherit(pass.raster);
}

OriginalGammaState decode_original_gamma(std::span<const std::uint8_t> bytes, int brightness) {
    if (bytes.size() < 1024) throw std::runtime_error("Original gamma resource is shorter than the source's 1024-byte LUT.");
    OriginalGammaState result;
    for (unsigned channel = 0; channel < 3; ++channel) {
        for (unsigned i = 0; i < 256; ++i) {
            const auto index = bytes[256 + channel * 256 + i];
            result.corrected[channel][i] = static_cast<std::uint16_t>(
                std::clamp(std::int64_t{bytes[index]} + brightness, std::int64_t{0}, std::int64_t{255}) * 256);
            result.defaults[channel][i] = static_cast<std::uint16_t>(
                std::clamp(std::int64_t{i} + brightness, std::int64_t{0}, std::int64_t{255}) * 256);
        }
    }
    return result;
}

MaterialTechnique material_from_effect(std::string_view filename) noexcept {
    if (filename.empty()) return T::FixedFunction;
    const auto separator = filename.find_last_of("/\\:");
    if (separator != std::string_view::npos) filename.remove_prefix(separator + 1);
    for (const auto& [name, technique] : effect_names)
        if (equal_filename(filename, name)) return technique;
    return T::Unknown;
}

std::optional<std::uint32_t> material_texture(const NativeMaterialState& state, TextureRole role) noexcept {
    const auto t = state.technique;
    switch (role) {
    case TextureRole::Color: return state.entity_skins[0];
    case TextureRole::Normal:
        if (t == T::NormalMix || t == T::CubeNormalMix) return state.entity_skins[1];
        if (t == T::NormalTOptShadow || t == T::NormalTSpecShadow ||
            t == T::CubeNormalOptim || t == T::CubeNormalCar) return state.material_skins[1];
        if (t == T::NormalOptim || t == T::NormalTileOptim || t == T::NormalTileSpec)
            return state.material_skins[0];
        return {};
    case TextureRole::Lightmap:
        if (t == T::ShadowMap || t == T::NormalTOptShadow || t == T::NormalTSpecShadow)
            return state.material_skins[0];
        return {};
    case TextureRole::Environment: return cube(t) ? state.material_skins[0] : std::nullopt;
    case TextureRole::TerrainLayer: return terrain(t) ? state.material_skins[0] : std::nullopt;
    case TextureRole::BumpDuDv: return t == T::Water ? state.material_skins[0] : std::nullopt;
    }
    return {};
}

bool material_requires_tangent(MaterialTechnique technique) noexcept {
    return technique >= T::NormalOptim && technique <= T::CubeNormalMix;
}
bool material_requires_uv1(MaterialTechnique technique) noexcept {
    return terrain(technique) || technique == T::NormalTOptShadow || technique == T::NormalTSpecShadow ||
           technique == T::ShadowMap || technique == T::Water;
}
bool material_uses_cube(MaterialTechnique technique) noexcept { return cube(technique); }
OriginalMaterialDrawState original_material_draw_state(const NativeMaterialState& state,
                                                       bool source_effects_enabled) noexcept {
    if (!source_effects_enabled) return {T::FixedFunction, {&state.source_pass, 1}};
    return {state.technique, {state.passes.data(), state.pass_count}};
}
bool material_has_required_textures(const NativeMaterialState& state, bool source_effects_enabled) noexcept {
    const auto t = original_material_draw_state(state, source_effects_enabled).technique;
    if (t == T::Unknown || !state.entity_skins[0]) return false;
    if (material_requires_tangent(t) && !material_texture(state, TextureRole::Normal)) return false;
    if (cube(t) && !material_texture(state, TextureRole::Environment)) return false;
    if ((t == T::NormalTOptShadow || t == T::NormalTSpecShadow || t == T::ShadowMap) &&
        !material_texture(state, TextureRole::Lightmap)) return false;
    if (t == T::Water && !material_texture(state, TextureRole::BumpDuDv)) return false;
    if (terrain(t)) {
        const auto layers = t >= T::Terrain5A ? 4u : 3u;
        for (unsigned i = 0; i < layers; ++i) if (!state.material_skins[i]) return false;
    }
    return true;
}

bool initialize_builtin_material_state(NativeMaterialState& state, std::string_view material_name) noexcept {
    const auto* constants = builtin_material(material_name);
    if (!constants) return false;
    apply_builtin_constants(state, *constants, false);
    // Fresh source object only: 0x10013520 -> 0x100318b0 clears skills, matMtl and EVENT.
    state.skills = {};
    state.skills_known_mask = 0xfffff;
    state.texture_transform = {};
    // 1001a240 identifies SKIN1-4 at +0xfc/+0x100/+0x104/+0x108.
    // Fresh 0x278-byte allocation clears them; 1001a140/1001ac50 never replace them.
    state.material_skins = {};
    state.material_skins_known_mask = 0xfu;
    state.source_material_assigned = true;
    state.source_material_initialized = true;
    state.material_constants_known = true;
    state.source_event = SourceMaterialEvent::None;
    state.source_event_known = true;
    state.source_flags = 0;
    // Recovered fresh engine MATERIAL constructor leaves TANGENT clear too.
    state.source_flags_known_mask = 0xf0000u;
    return true;
}

void configure_material_state(NativeMaterialState& state, std::string_view material_name,
                              std::uint32_t entity_flags, float entity_alpha_percent) noexcept {
    state.source_material_assigned = !material_name.empty();
    if (!assets::valid_source_profile(state.source_profile)) {
        state.technique = T::Unknown;
        state.material_constants_known = false;
        state.complete = false;
        return;
    }
    state.entity_alpha = std::clamp(entity_alpha_percent * 0.01f, 0.0f, 1.0f);
    state.raster = {};
    state.raster.blend = (entity_flags & (1u << 10)) != 0;
    if (state.raster.blend) {
        state.raster.source_blend = BlendFactor::SourceAlpha;
        state.raster.destination_blend = BlendFactor::InverseSourceAlpha;
    }
    if ((entity_flags & (1u << 12)) != 0) {
        // Provisional OVERLAY seed; bound bitmap flag 0x100 resolves the source gate.
        state.raster.alpha_comparison = AlphaComparison::GreaterEqual;
        state.raster.alpha_reference = 100.0f / 255.0f;
    }
    state.entity_samplers = {};
    state.material_samplers = {};
    // 0x100480b0 rejects VIEW.NOSHADER before the entire effect. Preserve the
    // base texture/UV0 and sampler; configure_source_bitmap_raster resolves alpha test.
    state.source_pass = {};
    state.source_pass.sampler = state.entity_samplers[0];
    state.source_pass.raster = state.raster;
    const auto t = state.technique;
    state.fixed_alpha_known = t == T::FixedFunction || t == T::EnvCubeMix
        || t == T::EnvCubeAlpha || t == T::ShadowMap || t == T::Water;
    // Static TRANSPARENT seed only; 0x10044d10 requires the prior-material/view/texture draw decision.
    state.fixed_alpha_modulates_diffuse = (entity_flags & 0x400u) != 0;
    if (material_requires_tangent(t)) {
        const auto sampler = address(cube(t) ? AddressMode::Clamp : AddressMode::Wrap);
        state.entity_samplers[0] = sampler;
        if (t == T::NormalMix || t == T::CubeNormalMix) state.entity_samplers[1] = sampler;
        else state.material_samplers[(t == T::NormalTOptShadow || t == T::NormalTSpecShadow || cube(t)) ? 1 : 0] = sampler;
    }
    if (t == T::CubeNormalOptim || t == T::CubeNormalCar || t == T::CubeNormalMix) {
        state.raster.blend = false; // Explicit FX AlphaBlendEnable=False, ZWriteEnable=True.
        state.raster.depth_write = true;
        state.raster.effect_override_mask = BlendEnable | DepthWrite;
        state.raster.source_blend = BlendFactor::One;
        state.raster.destination_blend = BlendFactor::Zero;
    }
    if (t == T::EnvCubeMix || t == T::EnvCubeAlpha) {
        state.raster.blend = t == T::EnvCubeAlpha;
        state.raster.depth_write = true;
        state.raster.effect_override_mask = BlendEnable | DepthWrite;
        state.material_samplers[0] = address(AddressMode::Clamp);
        if (state.raster.blend) {
            state.raster.source_blend = BlendFactor::SourceAlpha;
            state.raster.destination_blend = BlendFactor::InverseSourceAlpha;
        } else {
            state.raster.source_blend = BlendFactor::One;
            state.raster.destination_blend = BlendFactor::Zero;
        }
    }
    state.passes = {};
    state.pass_count = 1;
    state.passes[0].raster = state.raster;
    if (terrain(t)) {
        constexpr std::array<std::array<float, 5>, 3> scales{{
            {90, 15, 17.5f, 5, 130}, {120, 30, 35, 10, 130}, {90, 60, 70, 20, 130}
        }};
        const auto variant = (static_cast<unsigned>(t) - static_cast<unsigned>(T::Terrain4A)) % 3;
        state.pass_count = t >= T::Terrain5A ? 5 : 4;
        state.raster.blend = false;
        state.raster.depth_test = state.raster.depth_write = true;
        state.raster.effect_override_mask = BlendEnable | DepthTest | DepthWrite | AlphaTestEnable;
        state.raster.source_blend = BlendFactor::One;
        state.raster.destination_blend = BlendFactor::Zero;
        state.raster.alpha_comparison = AlphaComparison::Always; // Explicit FX AlphaTestEnable=False.
        for (unsigned i = 0; i < state.pass_count; ++i) {
            auto& pass = state.passes[i];
            pass.material_slot = static_cast<std::uint8_t>(i);
            pass.role = i == 0 ? TextureRole::Color : TextureRole::TerrainLayer;
            pass.color_uv = 1;
            pass.alpha_uv = 0;
            pass.uv_scale = {scales[variant][i], scales[variant][i]};
            pass.sampler = address(AddressMode::Wrap);
            pass.sampler.w = AddressMode::Wrap;
            pass.sampler.linear = true;
            pass.sampler.filter_explicit = true;
            pass.raster = state.raster;
            if (i != 0) {
                pass.raster.blend = true;
                pass.raster.source_blend = BlendFactor::InverseSourceAlpha;
                pass.raster.destination_blend = BlendFactor::SourceAlpha;
                pass.raster.effect_override_mask |= SourceBlend | DestinationBlend;
            }
        }
    } else if (t == T::ShadowMap) {
        state.passes[0].color_uv = 1;
    } else if (t == T::Water) {
        // Source 0x10043d90 writes TSS7..10; x87 retains 1 for BUMPENVMAT11.
        state.bump_environment_matrix = {1, 0, 0, 1};
        state.bump_matrix_known = true;
        state.passes[0].color_uv = 1;
        state.entity_samplers[0] = address(AddressMode::Wrap);
        state.entity_samplers[0].linear = true;
        state.entity_samplers[0].filter_explicit = true;
        state.material_samplers[0].linear = true;
        state.material_samplers[0].filter_explicit = true;
        state.raster.blend = false; // water_entskin.fx:13, explicit override.
        state.raster.depth_test = state.raster.depth_write = true;
        state.raster.effect_override_mask = BlendEnable | DepthTest | DepthWrite;
        state.raster.source_blend = BlendFactor::One;
        state.raster.destination_blend = BlendFactor::Zero;
        state.passes[0].raster = state.raster;
    }
    state.material_constants_known = material_name.empty() && t == T::FixedFunction;
    if (!state.source_material_assigned) {
        // NULL retains its identity, although its draw fallback is the builtin mat_unlit.
        apply_builtin_constants(state, builtin_material_constants.back(), state.model_material_present);
    } else if (const auto* constants = builtin_material(material_name)) {
        state.material_constants_known = true;
        // An already-bound D3DMATERIAL supplies draw colors, not source albedo.
        // Reconfiguration must not reconstruct live skills, matMtl, EVENT or flags.
        apply_builtin_constants(state, *constants, state.model_material_present);
        state.complete = material_has_required_textures(state);
        return;
    }
    const auto profile_id = (((state.source_profile.shq * 2 + state.source_shader_model2) * 2
        + state.source_cube_capable) * 2 + (state.source_profile.stq > 0));
    for (const auto index : material_constant_variant_buckets[profile_id]) {
        const auto& constants = material_constant_variants[index];
        if (!equal_filename(material_name, constants.name) || !constants.definition_known) continue;
        state.material_constants_known = true;
        state.material_albedo_percent = 50.2177734375f; // Constructor raw 0xc8df.
        state.material_albedo_known = true;
        // Source WDL MATERIAL factory 0x1001a140, prepared by 0x1001ba10.
        // A named WDL material replaces the MDL palette, not just its fields.
        state.ambient = {1, 1, 1, 1};
        state.diffuse = {1, 1, 1, 1};
        // Allocator 0x10013520 -> 0x100318b0 clears the entire 0x278-byte MATERIAL.
        state.specular = {0, 0, 0, 1};
        state.emissive = {0, 0, 0, 1};
        state.power = 0;
        state.power_known = true;
        state.skills = {};
        state.skills_known_mask = 0xfffff;
        state.texture_transform = {};
        state.ambient_alpha_known = true;
        state.color_known_mask = 0xfff;
        for (unsigned i = 0; i < state.skills.size(); ++i)
            if ((constants.skills_mask & (1u << i)) != 0) state.skills[i] = constants.skills[i];
        state.skills_known_mask |= constants.skills_mask;
        const std::array sources{constants.ambient, constants.diffuse, constants.specular, constants.emissive};
        const std::array destinations{&state.ambient, &state.diffuse, &state.specular, &state.emissive};
        for (unsigned color = 0; color < sources.size(); ++color) {
            for (unsigned channel = 0; channel < 3; ++channel) {
                if ((constants.color_mask & (1u << (color * 3 + channel))) != 0)
                    (*destinations[color])[channel] = static_cast<float>(
                        static_cast<double>(sources[color][channel]) * 0.003921568859368563);
            }
        }
        state.color_known_mask |= constants.color_mask;
        if (constants.power_known) { state.power = constants.power; state.power_known = true; }
        if (constants.albedo_known) {
            state.material_albedo_percent = constants.albedo;
            state.material_albedo_known = true;
        }
        if (constants.texture_factor_known) {
            state.texture_factor = constants.texture_factor;
            state.texture_factor_known = true;
        }
        state.environment_reflection_percent = constants.reflection_percent;
        state.environment_reflection_known = constants.reflection_percent_known;
        if (constants.reflection_percent_known) {
            // Acknex pixel_for_vec 0x10007070, format 8888 (raw 0x8ae000).
            // Source vector is (0,0,0); source percent is already exact Q10.
            const auto raw = static_cast<std::int32_t>(constants.reflection_percent * 1024.0f);
            const auto alpha_q10 = static_cast<std::uint32_t>((std::int64_t{raw} * 255) / 100);
            state.texture_factor = ((alpha_q10 & 0xfffffc00u) << 8) << 6;
            state.texture_factor_known = true;
        }
        break;
    }
    state.complete = material_has_required_textures(state);
}

} // namespace c4::rendering
