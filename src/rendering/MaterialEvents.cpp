#include "MaterialEvents.hpp"

namespace c4::rendering {
namespace {

constexpr char lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? char(value + ('a' - 'A')) : value;
}

bool same_name(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

std::optional<bool> initial_source_tangent(const NativeMaterialState& material,
                                         std::string_view material_name) noexcept {
    if (!assets::valid_source_profile(material.source_profile)) return std::nullopt;
    // Explicit MATERIAL.tangent=on writes in shader_uygula_strtr; not an FX
    // classification. Low branches retain the recovered fresh constructor zero.
    // c4_materials.wdl:1048,1095,1142,1189,1481,1537,1578,2539,
    // 5602,5650,6943,6998,7031,7093: SHQ>=1 and shaderVersion>=2020.
    static constexpr std::string_view shq1[] = {
        "Mtl_kul_car", "Mtl_kul_robot", "Mtl_siyah_car", "Mtl_siyah_robot",
        "Mtl_ic_simit", "Mtl_ic_panel", "Mtl_ic_tavan", "Mtl_arac_koltuk",
        "Mtl_car3", "Mtl_robot_cs2", "Mtl_araba_lastik", "Mtl_arac_fargovde",
        "Mtl_robot_fargovde", "Mtl_robot_kaporta"
    };
    // c4_materials.wdl:622-6926: remaining authored on writes are SHQ>=2
    // and shaderVersion>=2020. Cube/STQ gates occur after these tangent writes.
    static constexpr std::string_view shq2[] = {
        "Mtl_set_dur", "Mtl_loop_reklam", "Mtl_afis_site", "Mtl_askagel",
        "Mtl_bal_kopek", "Mtl_yeni_atmos_sag", "Mtl_yeni_atmos_sol", "Mtl_bal_kil",
        "Mtl_yeni_siteler", "Mtl_yeni_pal", "Mtl_insaat_bez", "Mtl_engel_2",
        "Mtl_yol_kum", "Mtl_tup_kald", "Mtl_tup_yol", "Mtl_y_kum_yan",
        "Mtl_vadi_yan", "Mtl_vadi", "Mtl_cikis", "Mtl_ex_yol_kald",
        "Mtl_SH_denekop_ek", "Mtl_SH_ex_yol", "Mtl_depo", "Mtl_beko",
        "Mtl_temel", "Mtl_dev_4", "Mtl_dev_3", "Mtl_dev_2", "Mtl_d2",
        "Mtl_cit", "Mtl_sahil", "Mtl_milyo", "Mtl_kon", "Mtl_kafa", "Mtl_bina_x",
        "Mtl_g2", "Mtl_gemi", "Mtl_duvar_wan", "Mtl_NFS", "Mtl_NFS2",
        "Mtl_cadir", "Mtl_adres", "Mtl_SH_gecis", "Mtl_SH_zgisealt",
        "Mtl_SH_kule2", "Mtl_SH_kule", "Mtl_SH_beton", "Mtl_SH_anayol",
        "Mtl_luna", "Mtl_POP", "Mtl_uc_ok", "Mtl_tabela_8_9", "Mtl_k_eyaz",
        "Mtl_koru", "Mtl_st_layt", "Mtl_zeplin", "Mtl_ucgen_2", "Mtl_ucgen",
        "Mtl_bez_kenar", "Mtl_gise_koru", "Mtl_duvar_ek", "Mtl_cubuk_2",
        "Mtl_bin_14", "Mtl_bin_13", "Mtl_bin_12_b", "Mtl_bin_12", "Mtl_ara_yol",
        "Mtl_pervane", "Mtl_agac", "Mtl_topkapi", "Mtl_t6", "Mtl_k2", "Mtl_k5",
        "Mtl_topak_y", "Mtl_DVR", "Mtl_bin_10", "Mtl_bin_10_lam", "Mtl_bin7",
        "Mtl_bin_8", "Mtl_bin_alt", "Mtl_bin_9", "Mtl_duvar_3", "Mtl_binalar_a",
        "Mtl_ev_2", "Mtl_ev_4", "Mtl_duvset1", "Mtl_duvset2", "Mtl_yol_ok",
        "Mtl_set1", "Mtl_balon", "Mtl_lastikler", "Mtl_bar4", "Mtl_don_2",
        "Mtl_stop", "Mtl_yantel", "Mtl_don_ok", "Mtl_33", "Mtl_kap2",
        "Mtl_tup_tunel_son", "Mtl_kapsul_ust2", "Mtl_kapsul1", "Mtl_kuleler",
        "Mtl_tup_tunel", "Mtl_siteler", "Mtl_rampa_01", "Mtl_drek", "Mtl_yan_oklar",
        "Mtl_gise_reklam", "Mtl_gise_cati", "Mtl_z_kulube2", "Mtl_duvar_a",
        "Mtl_w_duvar", "Mtl_bez", "Mtl_tel_x", "Mtl_tunel", "Mtl_orta_bariyel",
        "Mtl_z_bariyerler", "Mtl_willa_kapi", "Mtl_uc_bariyer", "Mtl_orta_bariyel2",
        "Mtl_bas_1", "Mtl_ins_kork", "Mtl_silindir", "Mtl_kule_tut",
        "Mtl_kopru_kork", "Mtl_r_kaporta", "Mtl_korkuluk", "Mtl_traf_levha",
        "Mtl_semsiye", "Mtl_tabela", "Mtl_bariyer", "Mtl_yol", "Mtl_tup_yol_son",
        "Mtl_tup_yol_dis", "Mtl_tup_kald_son", "Mtl_tup_kald_dis", "Mtl_gg",
        "Mtl_gg2", "Mtl_exyol_tunel", "Mtl_exyol_tun_kal"
    };
    for (const auto name : shq1)
        if (same_name(material_name, name))
            return material.source_profile.shq >= 1 && material.source_shader_model2;
    for (const auto name : shq2)
        if (same_name(material_name, name))
            return material.source_profile.shq >= 2 && material.source_shader_model2;
    // Complete active declarations have no tangent keyword or later on write:
    // c4_materials.wdl:44-68,218-222,283-287,395-399,467-471,529-575;
    // effect_define.wdl:130-143,220-226. Engine builtin constructor is likewise
    // recovered clear. Do not generalize this proof to an unlisted MATERIAL.
    static constexpr std::string_view off[] = {
        "Mtl_glow", "Mtl_arka_far", "Mtl_bayrak2", "Mtl_ghost", "Mtl_c_ball",
        "Mtl_kulube_cam", "Mtl_arac_cam", "Mtl_hell", "Mtl_spooot", "Mtl_tentem",
        "Mtl_vinc_mod", "mat_multi_anadolu", "mat_multi_avrupa", "mtl_denizwater",
        "mat_model", "mat_sprite", "mat_metal", "mat_terrain", "mat_unlit"
    };
    for (const auto name : off)
        if (same_name(material_name, name)) return false;
    return std::nullopt;
}

void water_iteration(WaterMaterialEventState& event, NativeMaterialState& material,
                     physics::Var delta) noexcept {
    // effect_define.wdl:232-235: shared Q10 skill addition, then floatd(skill,256).
    event.skill1 += delta;
    event.skill2 += delta;
    // Original floatd 0x100049f0 stores each Q10 operand as f32 before division.
    // Divisor 256 is exact; Var::to_float() is the recovered Q10 -> f32 boundary.
    material.texture_transform[8] = event.skill1.to_float() / 256.0f;
    material.texture_transform[9] = event.skill2.to_float() / 256.0f;
}

} // namespace

MaterialViewEvent source_material_view_event(std::string_view material_name) noexcept {
    // c4_materials.wdl active declarations; effect_define.wdl:613-646 replaces
    // the commented car/robot declarations at the top of c4_materials.wdl.
    static constexpr std::string_view unconditional[] = {
        "Mtl_arka_far", "Mtl_bayrak2", "Mtl_ghost", "Mtl_c_ball",
        "Mtl_kapsul_ust2", "Mtl_gise_reklam", "Mtl_kulube_cam",
        "Mtl_silindir", "Mtl_kule_tut", "Mtl_korkuluk", "Mtl_arac_farcam",
        "Mtl_ic_tavan", "Mtl_ic_panel", "Mtl_c4_ayna_22", "Mtl_tentem"
    };
    static constexpr std::string_view nonzero_skill1[] = {
        "Mtl_loop_reklam", "Mtl_robot_cs2", "Mtl_vinc_mod", "Mtl_afis_site",
        "Mtl_kul_car", "Mtl_kul_robot", "Mtl_siyah_car", "Mtl_siyah_robot",
        "Mtl_car3", "Mtl_robot_kaporta", "Mtl_arac_cam", "Mtl_hell",
        "Mtl_spooot", "Mtl_yeni_atmos_sag", "Mtl_yeni_atmos_sol"
    };
    for (const auto name : unconditional)
        if (same_name(material_name, name)) return MaterialViewEvent::Unconditional;
    for (const auto name : nonzero_skill1)
        if (same_name(material_name, name)) return MaterialViewEvent::NonzeroSkill1;
    if (same_name(material_name, "Mtl_tabela") || same_name(material_name, "Mtl_r_kaporta"))
        return MaterialViewEvent::CubeConversionOnly;
    return MaterialViewEvent::None;
}

void initialize_source_material_event_state(NativeMaterialState& material,
                                            std::string_view material_name) noexcept {
    material.source_material_initialized = true;
    material.source_event = SourceMaterialEvent::None;
    // Empty names select mat_unlit rather than an authored source MATERIAL.
    material.source_event_known = !material_name.empty() && material.material_constants_known;
    material.source_flags = 0;
    // 10013520 clears the fresh object; 1001a140 does not write +0xf0.
    // Authored MATERIAL definitions do not set these three default-launch bits.
    // Do not claim knowledge of unrelated cache/effect flags.
    material.source_flags_known_mask = material.source_event_known ? 0xe0000u : 0u;
    // Declared MATERIALs and their tangent writes exist independently of complete
    // effect/constants variants: c4_materials.wdl:144,353-357,401-405;
    // tangent=on at 1922,6550,6662 precedes shadow/cube gates at 1930,6558,6670.
    if (const auto tangent = initial_source_tangent(material, material_name)) {
        material.source_flags_known_mask |= 0x10000u;
        if (*tangent) material.source_flags |= 0x10000u;
    }
    if (!material.source_event_known) return;
    if (same_name(material_name, "mtl_denizwater")) {
        material.source_event = SourceMaterialEvent::WaterLoop;
        return;
    }
    switch (source_material_view_event(material_name)) {
    case MaterialViewEvent::Unconditional:
        material.source_event = SourceMaterialEvent::EnvironmentInitialize;
        break;
    case MaterialViewEvent::NonzeroSkill1:
        material.source_event = SourceMaterialEvent::EnvironmentInitializeForGatedView;
        break;
    case MaterialViewEvent::CubeConversionOnly:
        material.source_event = SourceMaterialEvent::EnvironmentCubeConversion;
        break;
    case MaterialViewEvent::None:
        break;
    }
}

std::optional<bool> source_material_tangent_request(
    const NativeMaterialState* entity_material,
    const NativeMaterialState* current_material) noexcept {
    constexpr std::uint32_t tangent = 0x10000u;
    bool known = true;
    for (const auto* material : {entity_material, current_material}) {
        if (!material) continue;
        if ((material->source_flags_known_mask & tangent) == 0) known = false;
        else if ((material->source_flags & tangent) != 0) return true;
    }
    return known ? std::optional<bool>{false} : std::nullopt;
}

std::string_view source_imported_mesh_material(std::string_view action) noexcept {
    static constexpr std::array mappings{
        std::array<std::string_view, 2>{"SHM_y_kum_yan", "Mtl_y_kum_yan"},
        std::array<std::string_view, 2>{"SHM_yol_kum", "Mtl_yol_kum"},
        std::array<std::string_view, 2>{"SHM_parca_2", "Mtl_parca_2"},
        std::array<std::string_view, 2>{"SHM_vadi_yan", "Mtl_vadi_yan"},
        std::array<std::string_view, 2>{"SHM_vadi", "Mtl_vadi"},
        std::array<std::string_view, 2>{"SHM_cikis", "Mtl_cikis"},
        std::array<std::string_view, 2>{"SHM_ex_yol_kald", "Mtl_ex_yol_kald"},
        std::array<std::string_view, 2>{"SHM_denekop_ek", "Mtl_SH_denekop_ek"},
        std::array<std::string_view, 2>{"SHM_ex_yol", "Mtl_SH_ex_yol"},
        std::array<std::string_view, 2>{"SHM_anayol", "Mtl_SH_anayol"},
        std::array<std::string_view, 2>{"SHM_beton", "Mtl_SH_beton"},
        std::array<std::string_view, 2>{"SHM_kule", "Mtl_SH_kule"},
        std::array<std::string_view, 2>{"SHM_kule2", "Mtl_SH_kule2"},
        std::array<std::string_view, 2>{"SHM_zgisealt", "Mtl_SH_zgisealt"},
        std::array<std::string_view, 2>{"SHM_gecis", "Mtl_SH_gecis"}
    };
    for (const auto& mapping : mappings)
        if (same_name(action, mapping[0])) return mapping[1];
    return {};
}

bool apply_source_imported_mesh_tangent_off(NativeMaterialState& material,
                                           std::string_view action) noexcept {
    if (source_imported_mesh_material(action).empty()) return false;
    material.source_flags &= ~0x10000u;
    material.source_flags_known_mask |= 0x10000u;
    return true;
}

bool source_material_default_event_eligible(const NativeMaterialState& material) noexcept {
    constexpr std::uint32_t guard = 0xe0000u;
    return material.source_event_known && material.source_event != SourceMaterialEvent::None
        && (material.source_flags_known_mask & guard) == guard
        && (material.source_flags & guard) == 0;
}

void initialize_water_material_source_defaults(WaterMaterialEventState& event,
                                               NativeMaterialState& material) noexcept {
    // mtl_create 100077f0 -> 1001a1c0 -> 10013520 -> 100318b0 clears 0x278
    // bytes. 1001a140 does not override skills +0x60..0xac or matMtl +0xb0..0xec.
    // effect_define.wdl:220-226 declares no authored skill/matrix initializer.
    event = {};
    material.texture_transform = {};
}

void start_water_material_event(WaterMaterialEventState& event,
                                NativeMaterialState& material,
                                physics::Var time_step) noexcept {
    ++event.active_processes;
    water_iteration(event, material, physics::v(0.5) * time_step);
}

void resume_water_material_event(WaterMaterialEventState& event,
                                 NativeMaterialState& material,
                                 physics::Var time_step) noexcept {
    water_iteration(event, material, physics::v(0.5) * time_step);
}

void update_environment_material_view(NativeMaterialState& material,
                                      const std::array<float, 16>& inverse_view) noexcept {
    // effect_define.wdl:49-52 / 59-62: mat_set followed by matrix41/42/43 = 0.
    material.texture_transform = inverse_view;
    material.texture_transform[12] = 0.0f;
    material.texture_transform[13] = 0.0f;
    material.texture_transform[14] = 0.0f;
}

} // namespace c4::rendering
