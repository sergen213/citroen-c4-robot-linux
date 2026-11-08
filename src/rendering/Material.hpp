#pragma once

#include "assets/SourceProfile.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace c4::rendering {

enum class MaterialTechnique : std::uint32_t {
    FixedFunction, NormalOptim, NormalTileOptim, NormalTileSpec, NormalMix,
    NormalTOptShadow, NormalTSpecShadow, CubeNormalOptim, CubeNormalCar,
    CubeNormalMix, EnvCubeMix, EnvCubeAlpha, ShadowMap,
    Terrain4A, Terrain4B, Terrain4C, Terrain5A, Terrain5B, Terrain5C, Water, Unknown
};
enum class TextureRole : std::uint8_t { Color, Normal, Lightmap, Environment, TerrainLayer, BumpDuDv };
enum class AddressMode : std::uint8_t { Wrap, Clamp, Mirror, Unspecified };
enum class BlendFactor : std::uint8_t { Zero, One, SourceAlpha, InverseSourceAlpha };
enum class AlphaComparison : std::uint8_t { Always, GreaterEqual };
enum EffectRasterOverride : std::uint8_t {
    BlendEnable = 1u << 0, DepthTest = 1u << 1, DepthWrite = 1u << 2,
    SourceBlend = 1u << 3, DestinationBlend = 1u << 4, AlphaTestEnable = 1u << 5
};

struct SamplerState {
    AddressMode u{AddressMode::Unspecified}, v{AddressMode::Unspecified}, w{AddressMode::Unspecified};
    bool linear{};
    bool filter_explicit{}; // False: FX inherits engine sampler filtering; not a parity assertion.
};
struct RasterState {
    bool blend{}, depth_test{true}, depth_write{true};
    BlendFactor source_blend{BlendFactor::One}, destination_blend{BlendFactor::Zero};
    AlphaComparison alpha_comparison{AlphaComparison::Always};
    float alpha_reference{};
    bool color_key{}; // Non-DDS/non-32-bit OVERLAY: exact top-left source pixel.
    bool defaults_recovered{}; // Original engine-inherited states require observation/recovery.
    std::uint8_t effect_override_mask{}; // Only renderstates explicitly authored by the FX pass.
};
struct MaterialPass {
    TextureRole role{TextureRole::Color};
    std::uint8_t material_slot{}; // 0: entSkin1, 1..4: mtlSkin1..4.
    std::uint8_t color_uv{}, alpha_uv{};
    std::array<float, 2> uv_scale{1, 1};
    SamplerState sampler;
    RasterState raster;
};

// 0x10044d10 reads the already-bound material before binding the selected one.
// Its ambient alpha is not the selected material's ambient alpha.
struct OriginalModelAlphaInput {
    bool prior_material_present;
    float prior_ambient_alpha;
    float view_alpha;
    bool texture_source_alpha_flag; // Current texture exists and source bitmap flags contain 0x100; not bpp.
    bool entity_transparent;
    float entity_alpha_percent;
    bool entity_bright_0x200000;
    bool source_additive_enabled; // Original global 0x106ed91c == 255.
    // Actual 0x10227eb8 != 0 preserves the bound VIEW material; ordinary root views use false.
    // When true, the caller supplies the bound material and its unchanged diffuse alpha.
    bool preserve_view_material_context;
    float prior_diffuse_alpha;
};
struct OriginalModelAlphaDecision {
    bool modulate_diffuse;
    bool alpha_blend;
    bool depth_write; // View fading preserves depth writes; material transparency disables them.
    BlendFactor source_blend, destination_blend; // Ignored when alpha_blend is false.
    float diffuse_alpha;
};
OriginalModelAlphaDecision original_model_alpha_decision(const OriginalModelAlphaInput& input) noexcept;
// Original model state binds first; authored FX pass renderstates take precedence.
RasterState apply_original_model_alpha(const RasterState& raster,
                                      const OriginalModelAlphaDecision& alpha) noexcept;

// Registered source MATERIAL callback, independent of the selected draw effect.
enum class SourceMaterialEvent : std::uint8_t {
    None, WaterLoop, EnvironmentInitialize, EnvironmentInitializeForGatedView,
    EnvironmentCubeConversion,
    EnvironmentView, // mtl_envmap_view1: unconditional matrix write.
    EnvironmentViewNonzeroSkill1 // mtl_envmap_view2: skill1 gates the view body only.
};

struct NativeMaterialState {
    assets::SourceProfile source_profile{};
    // Native GL2.1/Vulkan implement these source equivalents; not observed Windows CAPS.
    bool source_shader_model2{true}, source_cube_capable{true};
    bool source_material_assigned{}, model_material_present{};
    // Fresh initialization has run; this does not prove any source field.
    bool source_material_initialized{};
    SourceMaterialEvent source_event{SourceMaterialEvent::None};
    bool source_event_known{};
    // Raw source MATERIAL +0xf0 bits; the mask records only proven bits.
    std::uint32_t source_flags{}, source_flags_known_mask{};
    MaterialTechnique technique{MaterialTechnique::FixedFunction};
    std::array<std::optional<std::uint32_t>, 4> entity_skins, material_skins;
    // Per-slot source bitmap pointer presence/absence, not the effect's sampler value.
    std::uint8_t material_skins_known_mask{};
    std::array<SamplerState, 4> entity_samplers, material_samplers;
    std::array<float, 4> diffuse{1, 1, 1, 1}, ambient{}, specular{}, emissive{};
    float power{};
    std::array<float, 20> skills{}; // Source MATERIAL skills; never entity skills.
    std::uint32_t skills_known_mask{};
    std::uint16_t color_known_mask{}; // RGB field presence: ambient/diffuse/specular/emissive.
    bool power_known{}, material_albedo_known{}, texture_factor_known{};
    bool ambient_alpha_known{}; // MDL alpha or measured shader uploader, not RGB-only WDL settings.
    bool fixed_alpha_known{}; // Fixed operator support; not a complete draw-time alpha decision.
    bool fixed_alpha_modulates_diffuse{}; // TRANSPARENT seed only; render draws require original_model_alpha_decision.
    float environment_reflection_percent{}; // Exact source value before packed DWORD conversion.
    bool environment_reflection_known{};
    MaterialPass source_pass; // 0x10044d10 baseline, before Effect::Begin/BeginPass overrides.
    std::array<MaterialPass, 5> passes{};
    std::uint8_t pass_count{1};
    RasterState raster;
    float entity_alpha{0.5f}; // Original default is 50 percent; blend state gates its use.
    // Entity constructor 0x1003c2d0: ambient0/albedo50; distinct from MATERIAL albedo.
    float entity_ambient_percent{}, entity_albedo_percent{50}, material_albedo_percent{};
    std::uint32_t texture_factor{}; // Original EnvCube pixel_for_vec packed DWORD.
    std::array<float, 16> texture_transform{}; // Source matMtl, column-vector equivalent.
    std::array<float, 4> bump_environment_matrix{}; // Source BUMPENVMAT00,01,10,11.
    bool bump_matrix_known{};
    bool material_constants_known{}, complete{};
};

struct OriginalMaterialDrawState {
    MaterialTechnique technique;
    std::span<const MaterialPass> passes;
};
// VIEW.NOSHADER suppresses the whole effect, not just shader instructions.
OriginalMaterialDrawState original_material_draw_state(const NativeMaterialState& state,
                                                       bool source_effects_enabled) noexcept;

// 0x1004f280 ORs the actual ENTITY and currently bound MATERIAL tangent bits.
// Null pointers mean absent MATERIALs. Unknown present bits remain unknown unless
// another present MATERIAL has a proven on bit; the selected FX is irrelevant.
std::optional<bool> source_material_tangent_request(
    const NativeMaterialState* entity_material,
    const NativeMaterialState* current_material = nullptr) noexcept;

struct NativeGameVisualState {
    // Engine shader coordinates (x,z,y), not native world Z-up. Convert once
    // in the producer; MATERIAL skill5.xyz is already shader space and unchanged.
    std::array<float, 4> ambient{}, sun_color{};
    std::array<float, 3> sun_direction{}, view_position{};
    std::array<float, 3> sun_position{};
    bool sun_position_known{};
    std::array<float, 3> view_direction{}; // Original vecViewDir, not point-to-eye.
    std::array<float, 16> world_to_view{}; // Column-major original shader coordinates.
    bool view_inputs_known{};
    // Original root VIEW constructor ORs 0xc000. Frozen active-view producers
    // replace only proven bits; unknown bits must not become frame metadata.
    std::uint32_t source_view_flags{0xc000};
    std::uint32_t source_view_flags_known_mask{0xffffffffu};
    std::array<float, 4> fog_color{};
    float fog_start{}, fog_end{};
    bool fog_enabled{}, gamma_mapping_known{};
    std::uint8_t source_gamma{}; // Retain WMB value; not an invented exponent.
};
struct OriginalBloomState {
    bool enabled{};
    std::uint32_t width{256}, height{256};
    // Original var .280/.188/.469/.516/.094 -> truncate Q10 -> float(skill).
    float threshold{286.0f / 1024}, blur_width{192.0f / 1024}, highlight{480.0f / 1024},
          scene{528.0f / 1024}, glow{96.0f / 1024};
};
struct OriginalGammaState {
    std::array<std::array<std::uint16_t, 256>, 3> corrected{}, defaults{};
    bool corrected_active{};
};

// new_color.wdl consumes 1024 bytes from the original 1280-byte files; the
// unused trailing plane is retained in the file and does not enter the ramps.
OriginalGammaState decode_original_gamma(std::span<const std::uint8_t> bytes, int brightness);

MaterialTechnique material_from_effect(std::string_view filename) noexcept;
// Fresh named engine MATERIAL only; unknown/empty names return false without mutation.
// Seeds source fields once, before binding any active MDL palette. Draw configuration
// preserves live skills/matMtl/EVENT/flags and never infers a selected FX from the name.
bool initialize_builtin_material_state(NativeMaterialState& state, std::string_view material_name) noexcept;
void configure_material_state(NativeMaterialState& state, std::string_view material_name,
                              std::uint32_t entity_flags, float entity_alpha_percent = 50) noexcept;
// Run after binding a base bitmap whose actual source flag 0x100 is known.
// overlay_requested is the caller's effective OVERLAY, including sprite overrides.
// Authored FX AlphaTestEnable takes precedence; VIEW.NOSHADER uses source_pass.
void configure_source_bitmap_raster(NativeMaterialState& state, bool overlay_requested,
                                    bool source_alpha_flag) noexcept;
std::optional<std::uint32_t> material_texture(const NativeMaterialState& state, TextureRole role) noexcept;
bool material_has_required_textures(const NativeMaterialState& state,
                                    bool source_effects_enabled = true) noexcept;
bool material_requires_tangent(MaterialTechnique technique) noexcept;
bool material_requires_uv1(MaterialTechnique technique) noexcept;
// Original cube families always bind the environment to mtlSkin1 (index 0).
bool material_uses_cube(MaterialTechnique technique) noexcept;

} // namespace c4::rendering
