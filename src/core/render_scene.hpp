#pragma once

#include "settings.hpp"
#include "assets/World.hpp"
#include "assets/GpuImage.hpp"
#include "rendering/Material.hpp"
#include "rendering/SourceLighting.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace c4::ui { struct UiFrame; }
namespace c4::game { struct EffectFrame; }

namespace c4::rendering {

struct SourceLightingState;

struct MeshView {
    std::span<const assets::Vertex> vertices;
    std::span<const std::uint32_t> indices;
    std::uint64_t pose_revision{};
    std::span<const std::array<float, 2>> secondary_uv;
    std::span<const std::array<float, 4>> tertiary_uv;
    assets::TangentProvenance tangent_provenance{assets::TangentProvenance::Unknown};
    std::optional<std::size_t> tangent_binding;
};
enum class InstanceKind { World, Model, Image };
enum class SourceSpriteUnavailable {
    None, PrivateFlagsUnknown, CachedEulerUnknown, PrivateFlagsAndCachedEulerUnknown
};


struct Instance {
    std::uint32_t mesh_index{};
    std::optional<std::uint32_t> texture_index;
    std::array<float, 16> model_to_world{}; // Column major, original Z-up coordinates.
    std::string source;
    std::string material; // Source identity; native translation is described by material_state.
    std::array<float, 4> diffuse{1, 1, 1, 1}; // Neutral inspection when no source diffuse exists.
    std::uint32_t flags{}; // Original effective entity flags, not an invented alpha policy.
    InstanceKind kind{InstanceKind::Model};
    std::optional<std::uint32_t> source_entity; // Original WorldScene.entities index.
    std::optional<std::uint32_t> source_group;
    std::optional<std::uint32_t> source_skin; // Null means the original no-material sentinel.
    NativeMaterialState material_state;
    const SourceEntityLightingCache* source_lighting_cache{};
    bool source_sprite{}, source_sprite_state_known{};
    SourceSpriteUnavailable source_sprite_unavailable{};
    std::uint32_t source_cull_mode{1}; // Original RS22: NONE1, CW2, CCW3; sprite fan retains 012/023.
    bool source_sprite_no_depth_write{};
    std::optional<float> source_skin_ambient_alpha; // Actual flags0x10 MDL D3DMATERIAL override, not WDL ambient.
    bool source_skin_override_known{true};
    std::optional<OriginalModelAlphaDecision> original_alpha;
    float source_alpha_percent{50};
    bool native_prepared_inactive{}; // Cold cache slot, not an original entity flag.
    bool source_tangent_requested{}; // Counted in its shared full-group generation binding.
};

inline bool instance_visible(const Instance& instance) noexcept {
    return !instance.native_prepared_inactive && (instance.flags & (1u << 8)) == 0
        && (!instance.source_sprite || instance.source_sprite_state_known);
}

// Native raster shadows share the recovered draw transparency boundary.
// nullopt is the existing unsupported model-alpha case, not an opaque fallback.
inline std::optional<bool> raster_shadow_caster(const Instance& instance,
    MaterialTechnique technique, const RasterState& raster,
    const OriginalModelAlphaDecision* alpha) noexcept {
    if (!instance_visible(instance) || !raster.depth_test) return false;
    if (raster.color_key || raster.alpha_comparison != AlphaComparison::Always) return true;
    // Blended authored blobs/decals do not occlude a depth map as solid quads.
    if (raster.blend && !raster.depth_write) return false;
    if (material_requires_tangent(technique)) return true;
    if ((instance.flags & (1u << 10)) != 0) return false;
    if (alpha) return (!alpha->modulate_diffuse || alpha->diffuse_alpha >= 1) &&
        (!raster.blend || raster.destination_blend != BlendFactor::One);
    if (instance.kind != InstanceKind::Model && !raster.blend && raster.depth_write) return true;
    return std::nullopt;
}

inline std::string_view source_sprite_unavailable_reason(const Instance& instance) noexcept {
    switch (instance.source_sprite_unavailable) {
    case SourceSpriteUnavailable::PrivateFlagsUnknown: return "source per-draw private sprite cache flags are unknown";
    case SourceSpriteUnavailable::CachedEulerUnknown: return "source conditional sprite Euler cache is unknown";
    case SourceSpriteUnavailable::PrivateFlagsAndCachedEulerUnknown:
        return "source per-draw private sprite cache flags and conditional Euler cache are unknown";
    case SourceSpriteUnavailable::None: return {};
    }
    return {};
}

struct SceneCounts {
    std::uint64_t mesh_vertices{}, mesh_triangles{}, groups{}, materials{};
    std::uint64_t world_models{}, world_instances{}, world_entities{}, world_objects{};
    std::uint64_t world_vertices{}, world_triangles{};
    std::uint64_t sprite_entities{}, omitted_sprite_entities{}, hidden_entities{};
    std::uint64_t sprite_private_flags_unknown{}, sprite_cached_euler_unknown{};
    std::uint64_t unresolved_actions{}, lightmaps{};
    std::uint64_t unresolved_materials{};
};
struct PoseBinding {
    const assets::ModelGroup* group{};
    std::size_t vertices_index{};
    std::vector<std::uint32_t> mesh_indices;
    assets::AnimationSample sample{};
    std::optional<std::uint32_t> source_entity;
    std::uint32_t group_index{};
    bool sample_known{};
    std::optional<std::size_t> tangent_binding;
};

struct TangentBinding {
    std::span<const assets::Vertex> vertices;
    std::span<const std::uint32_t> original_indices; // Full group, never material-partition order.
    std::vector<std::uint32_t> mesh_indices;
    std::vector<std::array<float, 4>> channel;
    std::size_t request_count{};
    bool generated{};
};

struct SourceSpritePose {
    std::array<float, 3> origin{}, authored_angles{}, cached_angles{}, scale{1, 1, 1};
    std::uint32_t authored_flags{}, internal_flags{}, internal_flags_known_mask{};
    bool cached_angles_known{}; // Captured handler cache, never an unconditional authored-Euler substitute.
    bool operator==(const SourceSpritePose&) const = default;
};
struct SourceSpriteView {
    float pan{}, tilt{}, focal_x{}, focal_y{};
    std::uint32_t flags{}; // Actual per-VIEW CULL_CW bit0x20000; no facing dependence on VIEW flags.
    bool operator==(const SourceSpriteView&) const = default;
};
struct SpriteBinding {
    std::uint32_t source_entity{}, mesh_index{}, owned_mesh_index{}, width{}, height{}, instance_index{};
    SourceSpritePose previous_pose;
    SourceSpriteView previous_view;
    bool state_cached{};
    // Exact draw helper output, not an ordinary source reconciliation seed.
    std::array<float, 3> cached_angles_after_draw{};
    bool cached_angles_after_draw_known{};
};

// Declared SKY skycube1, not a WMB entity or an environment reflection cubemap.
struct SourceSkyFace {
    std::uint32_t mesh_index{}, texture_index{};
    NativeMaterialState material_state;
    OriginalModelAlphaDecision original_alpha{};
};
struct SourceSkyBinding {
    std::string resource;
    std::array<SourceSkyFace, 6> faces; // Original +6 strip order, never bmap_to_cubemap order.
    std::uint32_t first_face_width{}, first_face_height{};
    std::uint32_t authored_flags{0x08000000}, private_flags{0x00124000};
    SourceEntityLightingCache source_lighting_cache; // Declared sky RGB; no floor/chroma lookup.
    bool stage_enabled{}, stage_enabled_known{}; // Original global10227d00 admission.
};


struct RenderScene {
    // Moving preserves vector allocations and hence all views. Copying would not.
    RenderScene() = default;
    RenderScene(RenderScene&&) noexcept = default;
    RenderScene& operator=(RenderScene&&) noexcept = default;
    RenderScene(const RenderScene&) = delete;
    RenderScene& operator=(const RenderScene&) = delete;

    std::optional<assets::WorldScene> world;
    std::optional<assets::Model> model;
    std::vector<assets::Mesh> owned_meshes;
    std::vector<std::vector<assets::Vertex>> posed_vertices;
    std::vector<PoseBinding> pose_bindings;
    std::vector<TangentBinding> tangent_bindings;
    std::string animation;
    std::vector<std::vector<std::uint32_t>> material_indices;
    std::vector<assets::Image> textures; // Source metadata only; native GPU bytes remain authoritative.
    // Parallel numeric slots, loaded once. CPU expansion is an explicit diagnostic/fallback operation.
    std::vector<assets::GpuImage> gpu_textures;
    std::vector<std::uint32_t> referenced_texture_indices; // Includes construction-time future skins.
    std::vector<std::string> texture_sources;
    std::vector<MeshView> meshes;
    std::vector<Instance> instances;
    std::vector<SpriteBinding> sprite_bindings;
    std::optional<SourceSkyBinding> sky; // Construction-only; excluded from ordinary scene geometry.
    std::vector<SourceEntityLightingCache> entity_lighting_caches;
    SceneCounts counts;
    std::uint64_t hidden_unrenderable_entities{};
    std::array<float, 3> bounds_min{}, bounds_max{};
    bool has_bounds{};
    std::array<std::uint8_t, 768> skin_palette_rgb{};
    bool skin_palette_ready{};
    assets::SourceProfile source_profile{assets::original_source_profile};
    std::optional<assets::GpuImageRuntimeContext> gpu_runtime_context; // Fixed before asset loading/backend uploads.
    bool source_effects_enabled{true}; // Initial captured VIEW policy; later draws use FrameSnapshot.
    std::uint64_t source_draw_revision{}; // Advances only after an accepted source root VIEW alpha packet.
    std::uint64_t history_epoch{1}; // Native history generation; source camera remains authoritative.
};

inline bool authored_source_shadows(const RenderScene& scene) noexcept {
    return scene.source_profile.stq > 0;
}

struct CameraState {
    std::array<float, 16> view_projection;
    std::string_view provenance;
    std::array<float, 3> view_position{};
    std::array<float, 16> view{}, projection{}, inverse_projection{};
    float clip_near{}, clip_far{}; // Positive distances; view Z is negative in front of the camera.
    bool projection_known{}; // Native Z-up right-handed view, OpenGL clip depth [-1, 1].
    std::uint32_t source_view_flags{}, source_view_flags_known_mask{}; // DWORD facts, not Q10 or inferred complete flags.
};

struct SourceSkyFrame {
    CameraState camera; // Same selected VIEW/aperture; only sky clipping extends far to twice normal far.
    std::array<std::array<float, 16>, 6> model_to_world;
};

struct FrameSnapshot {
    // Borrowed read-only view, not a deep copy: owner must finish consumption before mutating source buffers.
    const RenderScene& scene;
    std::span<const Instance> instances;
    CameraState camera;
    const ui::UiFrame* overlay{};
    const NativeGameVisualState* visuals{};
    const OriginalBloomState* bloom{};
    const game::EffectFrame* effects{};
    const OriginalGammaState* gamma{};
    const SourceLightingState* source_lighting{};
    bool source_effects_enabled{true}; // Inspection enables translation; game supplies captured VIEW.NOSHADER.
    std::optional<SourceSkyFrame> sky; // Empty for inspection, hidden world, or unproven/disabled sky gates.
    std::uint64_t history_epoch{1}; // Explicit known transition, never inferred from per-frame pose changes.
};

RenderScene load_render_scene(const Settings& settings);
RenderScene load_render_scene(const Settings& settings, const assets::GpuImageRuntimeContext& context);
// Complete source-created entity/cache preparation before this move and before PhysicsWorld borrows it.
RenderScene load_render_scene(const Settings& settings, assets::WorldScene world);
RenderScene load_render_scene(const Settings& settings, assets::WorldScene world,
    const assets::GpuImageRuntimeContext& context);
void sample_selected_animation(RenderScene& scene, float percentage);
const assets::ModelGroup& source_model_group(const RenderScene& scene, std::uint32_t entity, std::uint32_t group);
void prepare_entity_animation(RenderScene& scene, std::uint32_t entity);
void sample_entity_pose(RenderScene& scene, std::uint32_t entity, std::uint32_t group, assets::AnimationSample sample);
// Recovered MATERIAL bit0x10000 only; no request inferred from shader inputs.
// Call after effective material changes and before backend consumption.
void ensure_source_tangents(RenderScene& scene, Instance& instance);
// Construction-only cache preparation: finish before backend texture uploads.
std::uint32_t cache_texture(RenderScene& scene, const std::filesystem::path& path, bool cube = false);
void prepare_source_sky(RenderScene& scene, const std::filesystem::path& resource_owner);
std::uint32_t cache_model_skin(RenderScene& scene, std::uint32_t entity, std::uint32_t group, std::uint32_t skin);
void configure_instance_material(RenderScene& scene, Instance& instance,
    std::string_view material_name, float alpha_percent);
void synchronize_source_sprite(RenderScene& scene, std::uint32_t entity,
    const SourceSpritePose& pose, const SourceSpriteView& view);
// Captured source root VIEW with no VIEW material context; not a generic fresh-VIEW constructor policy.
void capture_original_root_alpha(RenderScene& scene);
// Preserve contiguous source-entity batches in the immutable snapshot span.
SceneCounts submitted_scene_counts(const FrameSnapshot& frame);
// Per skin batch, the original's deferred membership: 0x1003d5e0 entity test plus the
// 0x100506e1 mixed-model filter by 0x1003d620. TRANSPARENT/0x8000 flags or MATERIAL
// +0xf0 bit0x400 defer every batch; otherwise, without OVERLAY, the entity bitmap is
// transparent by 0x1004f610: bitmap flag0x100 or D3DMATERIAL alpha < .999 (0x10190c38).
// Private0x80 comes from MDL/HMP loader 0x1005e610 (via 0x1003d6f0) and, for alpha
// bitmaps, from sprite constructor 0x1005fe90 (1005ff92/1005ff9c ORs 0x100000).
// ponytail: drawn skin stands in for the model's whole skin set; differs only when an
// entity skin override changes a non-mixed model's skin transparency.
inline bool source_deferred_transparent(const RenderScene& scene, const Instance& instance) {
    if (!instance.source_entity) return false; // Level geometry is not an entity list member.
    const auto& state = instance.material_state;
    if ((instance.flags & 0x8400u) || (state.source_flags & state.source_flags_known_mask & 0x400u)) return true;
    if (instance.flags & 0x1000u) return false; // OVERLAY keeps the opaque list.
    const auto skin = state.entity_skins[0];
    return (skin && scene.gpu_textures.at(*skin).source_alpha_flag) ||
        (instance.source_skin_ambient_alpha && *instance.source_skin_ambient_alpha < 0.9990000128746033f);
}

// 0x100552f0 entity draw order: list 0x10225ff0 in scene order, sky, then list
// 0x10225ff4 qsorted by 0x10053480 (D3D_ENTSORT default 1: squared camera distance, far first).
// Writes frame.instances indices, opaque prefix first; returns the opaque prefix length.
inline std::size_t source_entity_draw_order(const FrameSnapshot& frame, std::vector<std::uint32_t>& order) {
    order.clear();
    for (std::uint32_t i = 0; i < frame.instances.size(); ++i)
        if (!source_deferred_transparent(frame.scene, frame.instances[i])) order.push_back(i);
    const auto opaque = order.size();
    for (std::uint32_t i = 0; i < frame.instances.size(); ++i)
        if (source_deferred_transparent(frame.scene, frame.instances[i])) order.push_back(i);
    // 0x100535c0 stores float (camera - render origin)^2 at ENTITY+0x430.
    const auto distance = [&](std::uint32_t index) {
        const auto& m = frame.instances[index].model_to_world;
        const auto& eye = frame.camera.view_position;
        const float x = eye[0] - m[12], y = eye[1] - m[13], z = eye[2] - m[14];
        return x * x + y * y + z * z;
    };
    // ponytail: stable ties keep scene order; MSVC qsort tie order is not modelled.
    std::stable_sort(order.begin() + static_cast<std::ptrdiff_t>(opaque), order.end(),
        [&](std::uint32_t a, std::uint32_t b) { return distance(a) > distance(b); });
    return opaque;
}
CameraState inspection_camera(const RenderScene& scene, int width, int height);

} // namespace c4::rendering
