#pragma once

#include "Material.hpp"
#include "physics/NativeNumeric.hpp"

namespace c4::rendering {

enum class MaterialViewEvent : std::uint8_t {
    None, Unconditional, NonzeroSkill1, CubeConversionOnly
};

// Active source declarations, not inferred from the FX technique. ASCII names
// are case-insensitive; cube conversion alone does not enable a view callback.
MaterialViewEvent source_material_view_event(std::string_view material_name) noexcept;

// Call after configure_material_state ONLY for a freshly created source MATERIAL.
// source_material_initialized tracks that lifecycle, not definition completeness.
// The definition-known marker seeds EVENT and proven default-dispatch bits;
// authored TANGENT writes are known independently of effect/constants completeness.
// Alpha/FX reconfiguration must preserve subsequent callback and flag writes.
void initialize_source_material_event_state(NativeMaterialState& material,
                                            std::string_view material_name) noexcept;

// c4_materials.wdl:7437-7585: only these recovered import actions clear their
// declared shared MATERIAL's tangent bit after wait(1), before ent_importmesh.
// SHM_tup_yol/kald do not import and do not clear tangent. Unknown actions return
// an empty name / false without mutation. Apply to the declared MATERIAL cache,
// not an arbitrary entity override; all users of that identity inherit the write.
std::string_view source_imported_mesh_material(std::string_view action) noexcept;
bool apply_source_imported_mesh_tangent_off(NativeMaterialState& material,
                                           std::string_view action) noexcept;

// The caller supplies an actual non-null MATERIAL; 1001abc0 additionally requires
// a non-null registered EVENT and clear ENABLE_RENDER/VIEW/TREE (0xe0000).
bool source_material_default_event_eligible(const NativeMaterialState& material) noexcept;

// One state per shared source MATERIAL, not per entity. Seed skills from the
// actual source MATERIAL; shader float payloads in NativeMaterialState are not
// Q10 counters. Zero initialization alone is not evidence of source defaults.
struct WaterMaterialEventState {
    physics::Var skill1{}, skill2{};
    std::uint64_t active_processes{};
};

// Fresh mtl_denizwater, before any water_yeni assignment: the recovered MATERIAL
// factory clears all Q10 skills and all 16 matrix floats (not identity).
// Call only at creation of the shared material; other material fields survive.
void initialize_water_material_source_defaults(WaterMaterialEventState& event,
                                               NativeMaterialState& material) noexcept;

// water_yeni's successful branch, after wait(1), writes only matrix11/22.
// Preserve all other entries of the caller's actual source matrix.
inline void initialize_water_material_transform(NativeMaterialState& material) noexcept {
    material.texture_transform[0] = 40.0f;
    material.texture_transform[5] = 40.0f;
}

// Apply the successful water_yeni MATERIAL writes in source order. The bitmap
// slot is the actual profile-resolved MTI_denz_nor, prepared by the scene owner.
// ENTITY identity and VIEW.NOSHADER do not gate these writes or alter EVENT/flags.
inline void apply_source_water_material_action(NativeMaterialState& material,
                                               std::uint32_t mti_denz_nor_skin) noexcept {
    initialize_water_material_transform(material);
    material.material_skins[0] = mti_denz_nor_skin;
    material.material_skins_known_mask |= 1u;
    material.technique = MaterialTechnique::Water; // effect_load("water_entskin.fx").
}

// Call once per actual default-event launch, not per assignment expression or
// mesh. Source 1003a3a0 reconciles changed ENTITY material identity; 1001abc0
// requires a material/event and (flags & 0xe0000) == 0 (no RENDER/VIEW/TREE).
// Creation also has an explicit default launch path. The first body executes
// synchronously at that dispatch; each launch adds an independent wait(1)
// continuation sharing these MATERIAL counters. time_step is the source Q10
// engine payload, never elapsed renderer seconds.
void start_water_material_event(WaterMaterialEventState& event,
                                NativeMaterialState& material,
                                physics::Var time_step) noexcept;

// Execute exactly one already-waiting wait(1) continuation's loop body. The
// caller owns the actual eligible saved-MY record and its liveness/scheduler
// gates; renderer UI or phase guesses are not eligibility. A launch's first
// body must not also resume in that frame. This does not register a process.
void resume_water_material_event(WaterMaterialEventState& event,
                                 NativeMaterialState& material,
                                 physics::Var time_step) noexcept;

// mtl_envmap_view1, or the taken branch of mtl_envmap_view2. For view2 the
// caller must first test the actual source mtl.skill1 != 0. Called per source
// enable_view callback with the actual inverse-view matrix, column-major.
void update_environment_material_view(NativeMaterialState& material,
                                      const std::array<float, 16>& inverse_view) noexcept;

} // namespace c4::rendering
