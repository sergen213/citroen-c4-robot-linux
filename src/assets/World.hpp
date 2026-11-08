#pragma once

#include "Animation.hpp"
#include "GpuImage.hpp"
#include "SourceProfile.hpp"

#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace c4::assets {
inline constexpr std::uint32_t no_world_model = std::numeric_limits<std::uint32_t>::max();

// Original Acknex coordinates (Z up), angles in degrees (pan, tilt, roll).
// Loaded placements retain disk values; parsing does not execute entity actions.
struct WorldEntity {
    std::uint32_t object_index{}, model_index{no_world_model}, flags{};
    std::array<float, 3> position{}, angles{}, scale{};
    std::array<float, 20> skills{};
    std::uint32_t source_flags_set{}, source_flags_clear{};
    std::string name, resource, action, material, string1, string2;
    float ambient{}, albedo{};
    // http://manual.conitec.net/aentity-alpha.htm: documented default 50 percent.
    float alpha_percent{50};
    std::optional<float> source_ambient, source_albedo;
    std::uint32_t path{}, attached_entity{};
    bool old_record{};
    // Runtime-created entities may retain original signed Q10 Var coordinates.
    // Disk placements remain floats and leave this unset; do not requantize them here.
    std::optional<std::array<std::int32_t, 3>> source_position_words;
    // Native preallocated helper slot, not an original engine flag or live actor.
    // Birth clears this before render/query participation; disk entities stay false.
    bool native_prepared_inactive{};
    // Proven current MATERIAL after WMB editor lookup, before actor dispatch.
    // Views refer to immutable registration names; known empty means actual NULL.
    std::string_view source_initial_material_identity{};
    bool source_initial_material_known{};
    // Authored static action's direct my.material write BEFORE its first wait1.
    // Not an editor identity or a helper/dynamic assignment; publish on queued entry.
    std::string_view source_action_material_before_wait{};
    // Normal construction zeroes prior reconciled ENTITY+0x3a4; mode3 never commits it.
    // This is independent of current MATERIAL and the special +0x3d4 snapshot.
    std::string_view source_previous_material_identity{};
    bool source_previous_material_known{};
};

struct WorldModel {
    std::filesystem::path resource;
    std::shared_ptr<const Model> model; // Unique original geometry, retained for collision/animation.
    std::filesystem::path morphed_resource;
    std::shared_ptr<const Model> morphed_model; // Actual action-selected ent_morph model, when present.
    std::filesystem::path imported_resource, texture_resource;
    Mesh imported_mesh; // Only an actual action's ent_importmesh replacement, never a fallback.
    std::vector<std::vector<std::filesystem::path>> skin_resources;
    std::string material;
    std::array<std::filesystem::path, 4> material_skin_resources;
    // Recovered MATERIAL bitmap slots, including authored NULL; not effective FX sampler values.
    std::uint8_t material_skins_known_mask{0};
    // Selected authored cube bitmap recipes; not source callback execution timing.
    std::uint8_t material_skin_cube_mask{0};
    std::filesystem::path effect_resource; // Source identity only; HLSL is not executed natively.
    Image sprite; // Original image entity; sprite orientation is not inferred here.
    // Original encoded upload storage; CPU sprite pixels remain for geometry/physics.
    std::optional<GpuImage> sprite_gpu;
};

struct SourceMaterialIdentity {
    std::string_view identity{};
    bool known{};
};

struct WorldMaterialResources {
    std::string_view name; // Stable view into the authored immutable binding table.
    std::array<std::filesystem::path, 4> skins;
    // Recovered MATERIAL bitmap slots, including authored NULL; not effective FX sampler values.
    std::uint8_t material_skins_known_mask{0};
    // Selected authored cube bitmap recipes; not source callback execution timing.
    std::uint8_t material_skin_cube_mask{0};
    std::filesystem::path effect;
    bool definition_known{};
};

struct WorldScene {
    SourceProfile source_profile{}; // Actual original selectors, not native GPU quality presets.
    std::vector<std::string> unresolved_actions; // Exact source behavior not executed by inspection.
    std::vector<std::string> unresolved_materials;
    std::vector<WorldEntity> entities;
    std::vector<WorldModel> models; // Unique resource/action binding cache; entities hold indices.
    std::array<std::uint8_t, 768> palette{}; // Original BGR palette, when present.
    bool has_palette{};
    std::uint32_t object_count{}, info_flags{}, sun_color{}, ambient_color{};
    std::array<std::uint32_t, 4> fog_colors{};
    float compiler_version{}, sun_azimuth{}, sun_elevation{};
    std::uint8_t gamma{}, lightmap_size_code{};
    // Supported parsed empty sections prove the ordinary source Newton world root absent.
    // This does not imply absence of external MDL/sprite entity collision geometry.
    bool source_empty_collision_root_known{};
};

// Original 7.01 entity-only WMB7 dialect (19 lists). All section/offset/record
// boundaries are checked. Unsupported nonempty geometry sections fail explicitly.
// Separating parsing from resource resolution permits malformed-input tests without assets.
WorldScene parse_world(std::span<const std::uint8_t> bytes);
// Resolves actual original archives next to the selected archive/loose WMB.
// Never executes WDL or manufactures missing meshes, textures, or object placements.
// Ordinary original C4 source-game startup: 1003acb0/1004a280/10044d10.
// Other known startup modes must pass their actual first-upload options explicitly.
inline constexpr GpuImageUploadOptions original_world_sprite_upload{true, 16, 32};
// Default source selectors are the observed original all-zero configuration.
WorldScene load_world(const std::filesystem::path& path,
    SourceProfile profile = original_source_profile,
    const GpuImageUploadOptions& sprite_upload = original_world_sprite_upload);
// Batch resolution builds one resource directory, not one scan per skin.
std::vector<std::vector<std::filesystem::path>> resolve_model_skins(
    const std::filesystem::path& owner, const Model& model,
    SourceProfile profile = original_source_profile);
// Owner is a loose resource or archive::member, not a directory. Resolves the
// entire batch from one neighboring-resource catalog, preserving canonical case.
std::vector<std::filesystem::path> resolve_resources(
    const std::filesystem::path& owner, std::span<const std::string_view> names,
    SourceProfile profile = original_source_profile);
// Pre-cache source controller material variants in one batch, never per frame.
std::vector<WorldMaterialResources> resolve_material_resources(
    const std::filesystem::path& owner, std::span<const std::string_view> names,
    SourceProfile profile = original_source_profile);
// Original resource+public-flag selection for CURRENT-NULL repair before reconcile.
// Ordinary authored MDL/sprite origins only; never replace a non-NULL current value.
SourceMaterialIdentity source_resource_material_identity(
    const WorldModel& model, std::uint32_t source_flags,
    SourceProfile profile = original_source_profile);
// Proven fresh resource-attachment recipe, not a material event or live birth.
// Manual/unvalidated models remain unknown. Callers publish only at actual creation.
SourceMaterialIdentity source_fresh_material_identity(
    const WorldModel& model, SourceProfile profile = original_source_profile);
std::array<float, 16> world_entity_transform(const WorldEntity& entity);
} // namespace c4::assets
