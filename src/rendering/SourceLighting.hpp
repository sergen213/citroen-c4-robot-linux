#pragma once

#include "Material.hpp"
#include "physics/Geometry.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace c4::assets { struct WorldScene; }

namespace c4::rendering {

struct CameraState;

// D3DLIGHT9 values after the original engine's conversion, in engine X,Z,Y
// shader/world axes. No source percentages, packed colors or selection heuristic
// are accepted at the GPU boundary.
struct alignas(16) SourceLight {
    std::array<float, 4> diffuse{}, specular{}, ambient{};
    std::array<float, 4> position_range{}, direction_type{};
    std::array<float, 4> attenuation_falloff{};
    std::array<float, 4> cone_enabled{}; // theta, phi (radians), enabled, reserved.
};
static_assert(sizeof(SourceLight) == 112);

struct SourceLightingState {
    std::array<SourceLight, 8> lights{};
    std::array<float, 4> global_ambient{}, fog_color{};
    std::array<float, 16> world_to_view{};
    std::array<float, 4> view_position{}, fog_params{}; // start,end,density,reserved.
    std::uint32_t light_count{};
    std::uint32_t sun_slot_plus_one{}; // Proven source SUN slot + 1; zero means unproved/no SUN.
    // Effective VIEW +50 integer after the source shift/capability clamp.
    std::int32_t view_ambient{};
    bool view_ambient_known{};
    bool sun_position_known{}; // CPU provenance only; directional lighting does not consume position.
    bool lights_known{}, ambient_known{}, fog_known{}, view_known{};
    bool source_effects_enabled{true}, source_effects_known{};
    bool fog_enabled{}, range_fog{};
    std::uint32_t fog_mode{}; // D3DFOGMODE: NONE0,EXP1,EXP2,LINEAR3.
};

// Renderer-owned inputs for a fresh native-equivalent ambient capability
// evaluation, not a snapshot of the original device's sticky historical bit20.
// Numeric defaults never establish shader support, device limits or LUT bytes.
struct SourceLightingDeviceState {
    std::uint32_t sourceMaxTextureWidth{};
    bool supportedADD{}, supportedMOD2X{}, capabilitiesKnown{};
    std::array<std::uint8_t, 256> sourceLUT{};
    bool sourceLUTKnown{};
};

bool source_ambient_direct_capable(const SourceLightingDeviceState& device) noexcept;
// Source constructor GAMMA raw1126, separate from the device's gamma ramp.
// Reproduces stored-f32 boundaries using host libm, not original-CRT bit parity.
std::array<std::uint8_t, 256> source_default_ambient_lookup() noexcept;

// Per-entity draw cache, not frame ambient. Constructor values are retained
// numerically, but only an actual producer may assert the corresponding facts.
struct SourceEntityLightingCache {
    std::int16_t environment{127}; // ENTITY private +49c.
    std::array<std::int16_t, 3> chroma_bgr{}; // ENTITY private +49e/+4a0/+4a2.
    std::array<std::int32_t, 3> source_color_raw_bgr{128 * 1024, 128 * 1024, 128 * 1024}; // Public Q10 BGR.
    std::int32_t light_range_raw{}; // Public Q10 ENTITY +50.
    bool environment_known{}, chroma_known{}, source_color_known{};
    bool light_range_known{};
};

// Source Q10 control words. Only SHADOW_MODE needs a known value for the
// miss/skip invariant; retained TRACE_FLOOR/POLY words do not certify their
// final original globals. Generic/manual provenance remains unknown.
struct SourceFloorTraceControls {
    std::int32_t shadow_mode_raw{}, trace_floor_raw{}, enable_polycollision_raw{};
    bool shadow_mode_known{}, normal_world_load_known{}; // level_load resource100.
};

// Cold qualification for the normal source WMB7 world-load lifecycle (resource
// flag100), never an arbitrary/plugin-mutated collision world. The parser's
// explicit empty-root fact and decoded entity resource classes are required.
bool source_scene_no_floor_hits(const assets::WorldScene& world,
                                const SourceFloorTraceControls& controls) noexcept;
// Independent cold proof for 10050570/1004f750: validated ordinary disk MDLs
// cannot activate RESOURCE1000; empty source section17 excludes embedded linked
// models. Manual/unclassified primary or morphed models remain unsupported.
bool source_scene_no_linked_brightness_writes(const assets::WorldScene& world) noexcept;
// Call once at actual source entity creation, before publishing cache pointers.
// A miss never calls this again: the private environment/chroma are persistent.
// ENV additionally requires proof that independent 1004f750 resource-brightness
// updates preserve its constructor value; a floor miss alone proves only chroma.
void initialize_source_entity_lighting_caches(std::span<SourceEntityLightingCache> caches,
                                              bool scene_no_floor_hits,
                                              bool independent_brightness_preserves_constructor) noexcept;
// Already-effective VIEW integer, not Q10: the sender owns shift/capability clamp.
void publish_source_view_ambient(SourceLightingState& state,
                                std::int32_t effective_ambient, bool known) noexcept;
void publish_source_entity_light_color(SourceEntityLightingCache& cache,
                                      const std::array<std::int32_t, 3>& raw_bgr,
                                      bool known) noexcept;
void publish_source_entity_light_range(SourceEntityLightingCache& cache,
                                      std::int32_t raw_range, bool known) noexcept;

// One exact std140 payload shared by OpenGL binding15 and Vulkan set0/binding15.
// controls: lighting, normalizeNormals, specularEnable, localViewer.
// status: complete, lightCount, fogEnable, fogMode; options: rangeFog,
// drawAlphaKnown, actual D3DTOP alpha operation (SELECTARG1=2 or MODULATE=4), sourceSunSlotPlus1.
struct alignas(16) SourceLightingUniforms {
    std::array<SourceLight, 8> lights{};
    std::array<float, 16> world_to_view{};
    std::array<float, 4> view_position{}, global_ambient{}, fog_color{}, fog_params{};
    std::array<float, 4> diffuse{}, ambient{}, specular{}, emissive{}, material_params{};
    std::array<std::uint32_t, 4> controls{}, status{}, options{};
};
static_assert(sizeof(SourceLightingUniforms) == 1152);

NativeGameVisualState source_game_visual_state(const assets::WorldScene& world,
                                               physics::Vec3 camera_position,
                                               physics::Vec3 camera_angles);
NativeGameVisualState source_inspection_visual_state(const assets::WorldScene& world,
                                                     const CameraState& camera);
std::array<float, 16> source_inverse_view_matrix(const NativeGameVisualState& visuals);
SourceLightingState source_lighting_state(const assets::WorldScene& world,
                                         const NativeGameVisualState& visuals) noexcept;
SourceLightingUniforms source_lighting_uniforms(const SourceLightingState& state,
                                                const NativeMaterialState& material,
                                                std::uint32_t entity_flags,
                                                const OriginalModelAlphaDecision* alpha = nullptr,
                                                const SourceEntityLightingCache* entity_cache = nullptr,
                                                const OriginalMaterialDrawState* draw_state = nullptr,
                                                const SourceLightingDeviceState* device = nullptr) noexcept;
// Original HUD VIEW is cam_ibre at the source origin. Current gauge assets use
// no source MATERIAL and no MDL material palette: mat_unlit, albedo zero.
// Any lighting-dependent material remains incomplete without its VIEW facts.
SourceLightingUniforms source_view_entity_uniforms(const NativeMaterialState& material,
                                                   std::uint32_t entity_flags,
                                                   const OriginalModelAlphaDecision* alpha = nullptr) noexcept;

// The common GLSL is authored in source_lighting.glsl and embedded alongside
// the Original material helpers by the existing shader generation step.

} // namespace c4::rendering
