#pragma once

#include "assets/World.hpp"
#include "audio/Audio.hpp"
#include "physics/PhysicsWorld.hpp"
#include "physics/NativeNumeric.hpp"
#include "WorldLeaves.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace c4::game {
using EntityId = std::uint32_t;
inline constexpr EntityId no_entity = physics::no_entity;
using physics::Vec3;
using Var = physics::Var;
using ScriptVec = physics::ScriptVec;
inline bool source_wait1_context_ready(Var freeze_mode, Var time_frame, bool saved_my_is_null) noexcept {
    return freeze_mode.raw < 2048 && (freeze_mode.raw < 1024 || saved_my_is_null) && time_frame.raw > 0;
}
class SourceEffects;
enum class PhaseRequest { none, robot, car };
enum class GamePhase { menu, selection, intro, countdown, race, paused, results };
enum class UiCommand { none, play, select_local, select_web, resume, restart, menu, help, close_help, quit };
struct EffectRequest {
    std::string_view helper;
    Vec3 position{}, normal{};
    EntityId entity{no_entity};
    Var argument{};
};
struct InputSnapshot {
    bool key_cuu{}, key_cud{}, key_cul{}, key_cur{}, key_d{}, key_s{}, key_space{}, key_c{}, key_esc{}, key_f1{}, mouse_right{};
    bool key_esc_pressed{}, key_f1_pressed{};
    Var mouse_force_x{};
    UiCommand ui_command{UiCommand::none};
};
struct FrameTiming {
    Var originaltime_step_ticks{Var::literal(0.5)};
    std::uint64_t frame_index{};
    double seconds_deadlines{};
    Var originaltime_frame_ticks{Var::literal(0.5)};
};
struct SourceViewSnapshot {
    ScriptVec position{}, angles{};
    Var arc{}, clip_near{}, clip_far{};
    double effective_near{}, pixel_aspect{};
    bool no_shader{}, no_particles{};
    std::uint32_t flags_value{}, flags_known_mask{};
    std::int32_t effective_ambient{};
    bool effective_ambient_known{};
};
struct VisualState {
    bool invisible{}, transparent{};
    Var alpha{Var::literal(100)};
    int skin{1};
    Var frame{Var::literal(1)}, next_frame{};
    assets::AnimationSample sample{};
    std::uint64_t revision{};
    std::string_view material_identity{};
    bool material_identity_known{};
};
struct ComponentIds {
    EntityId p_aracgovde{no_entity}, p_camera_target{no_entity};
    EntityId p_arac_cam{no_entity}, p_far_govde{no_entity}, p_far_cam{no_entity}, p_arac_ontakim{no_entity}, p_arac_koltuk{no_entity}, p_arac_kaporta{no_entity}, p_arac_direx{no_entity}, p_arac_ayna{no_entity}, p_arac_tekerler{no_entity};
    EntityId p_arac_arkafar{no_entity}, p_arac_arkaglow{no_entity}, p_fsolteker{no_entity}, p_fsagteker{no_entity}, p_bsolteker{no_entity}, p_bsagteker{no_entity}, p_arac_golge{no_entity};
    EntityId p_ic_camlar{no_entity}, p_ic_turbo{no_entity}, p_ic_panel{no_entity}, p_ic_tavan{no_entity}, p_ic_simit{no_entity}, p_ic_rakam0{no_entity}, p_ic_rakam1{no_entity}, p_ic_rakam2{no_entity};
    EntityId p_robot_teker{no_entity}, p_robot_mekanik{no_entity}, p_robot_farcam{no_entity}, p_robot_fargovde{no_entity}, p_robot_camlar{no_entity}, p_robot_kaporta{no_entity}, p_robot_golge{no_entity};
    EntityId p_ghost_arac{no_entity}, p_ghost_robo{no_entity}, p_zamanlama{no_entity};
    std::array<EntityId,24> path_ent_dizi{};
    std::vector<EntityId> pickups;
    ComponentIds() { path_ent_dizi.fill(no_entity); }
};
struct SharedState {
    Var ghost_reset_ok{Var::literal(1)}, ghost_yaris_start{}, game_over_ok{}, gameover_panel_acik{}, level_restart_no{}, smart_menu_islem{Var::literal(1)}, freeze_mode{}, oyunici_panel_acik{};
    Var oyun_oynanabilir{};
    Var camera_tip_no{}, camera_tipno{}, camrec_regno{}, model_robot_ok{}, arac_durum_tip{};
    Var arac_ivme{}, arac_max_hiz{Var::literal(350)}, arac_gidis_pan{}, arac_gidis_tilt{};
    ScriptVec arac_cekimkuvveti{};
    Var arac_turbo{Var::literal(1)}, start_turbo{}, turbo_ekle_miktar{}, c4_leftright_yon{};
    Var arac_vites_no{};
    Var arac_vites_ivme{}, motor_ses_ivme{}, motor_ses_volume{}, c4_vites1_time{}, c4_vites7_time{};
    Var arac_pati_aktif{};
    Var yercekimi_gucu{Var::literal(11)}, yc_alt_mesafe{}, c4_guncel_havazaman{}, c4_eski_havazaman{}, c4_havadongu_no{}, c4_dusme_dist{}, c4_yere_carpma{Var::literal(100)};
    Var toprak_yol_ok{};
    Var fe_donmeivme_time{Var::literal(600)}, fe_toparlaivme_time{Var::literal(600)}, fe_max_donmehiz{Var::literal(4.108)}, fe_hiz_donmeazal{Var::literal(83.210)}, fe_yol_tutusmik{Var::literal(1.441)}, fe_hiz_tutusazal{Var::literal(71.836)}, fe_hiz_gazbirak{Var::literal(458)};
    Var collision_ok{};
    ScriptVec crash_angle{}, crash_target{};
    Var crash_hedef{Var::literal(-666)}, crash_miktar{}, crash_totaltime{};
    Var bombacam_basla_t{}, bombacam_basla_z{}, bombacam_basla_m{}, bombacam_salla_s{}, bombacam_salla_m{};
    Var bombacam_salla_id{};
    Var jump_delay_time{}, temp_ghostframe{}, robot_action_no{}, robot_frame_no{}, r_kayma_fark{};
    std::array<Var,24> path_dist_diz{};
    std::array<Var,4> path_top_no{};
    EntityId selected_path{no_entity}; // The actual source body.skill70 handle.
    Var body_skill2{};
    Var cam_path_pan{}, cam_path_tilt{}, cam_path_up{}, cam_path_xy{}, cam_path_dist{}, cam_path_ivme{Var::literal(1)};
    Var checkpoint_no{}, checkpoint_oldframe{};
    std::array<Var,9> t_point_zaman{};
    Var t_oyun_zaman{}, t_oyun_bonus{};
    PhaseRequest phase_request{PhaseRequest::none};
    GamePhase phase{GamePhase::menu};
    bool quit_requested{};
    bool noparticle{}, noshader{}; // Primary camera.VIEW render gates, not source simulation/birth gates.
    bool bloom_noshader{}, bloom_noparticle{}; // Secondary camView.VIEW render gates.
    ScriptVec camera_position{}, camera_angles{};
    Var camera_arc{Var::literal(60)}, camera_clip_near{}, camera_clip_far{Var::literal(112500)};
    Var camera_ambient{}, bloom_camera_ambient{};
    double camera_effective_near{}, camera_pixel_aspect{}; // Effective native FLOAT matrix clamp/aspect, not raw VIEW properties.
    // Native temporal-history signal only; never changes original source variables.
    // Actual restart/camera-mode/intro/chase-start events advance it, not moving poses.
    std::uint64_t history_epoch{1};
    Var race_lap{Var::literal(1)};
    std::uint64_t rng_calls{};
    Var min_ghost_dist{}, black_alpha{};
    bool black_visible{}, hud_speed_visible{true}, finish_buttons_visible{};
    bool local_ghost_available{};
    Var local_ghost_ticks{};
    std::string local_ghost_name, record_error;
    Var selected_ghost{};
    Var bloom_aktif{};
    Var bloom_fx_ok{Var::literal(0)};
    ScriptVec bloom_camera_position{}, bloom_camera_angles{};
    Var bloom_camera_arc{Var::literal(60)};
    std::uint32_t rng_state{1}; // Native deterministic scope, not unproven engine RNG parity.
    audio::Handle carp_loop_handle{}, carp_ses1_handle{}, carp_ses2_handle{};
    Var c4_texture_rengi{};
    Var GA_Volume{Var::literal(100)}, GA_Parlaklik{};
    Var GA_MusicOk{Var::literal(1)};
    Var mouse_mode{Var::literal(0)}, d3d_anisotropy{Var::literal(0)};
    bool gamma_corrected_active{}; // Native latch updated by actual source gamma calls.
    std::string oyuncu_ismi;
    Var car_settling_frame{Var::literal(27.273)}, car_settling_speed{Var::literal(1)};
    audio::Handle music_handle{};
    audio::Handle ses_sokakhandle{};
    audio::Handle turbo_ses_handle1{}, turbo_ses_handle2{};
    audio::Handle helikopter_handle{};
    audio::Handle ses_loadhandle{};
    bool help_visible{}, hud_visible{true}, active_ghost{};
    Var hud_vites_frame{}, hud_motor_frame{}, gauge_needle_roll{};
    Var ghost_rekor{}, checkpoint_delta_ticks{};
    std::array<Var,9> ghost_point_time{};
    std::uint64_t checkpoint_event_counter{};
    std::uint64_t hud_off_event_counter{};
    int checkpoint_event_no{};
    std::string rakip_ismi{" "}, timepanel_time_str{"ZAMAN 00:00.000"}, timepanel_rekor_str{"REKOR --:--.---"}, timepanel_lap_str{"TUR   1/2"}, timepanel_check_str{" "};
    std::array<EffectRequest,128> effects{};
    std::size_t effect_count{};
    Var particle_count{Var::literal(1)};
    bool loading_visible{};
    int loading_stage{};
    Var loading_progress{};
    Var level1_yukleme_ok{};
    std::vector<SourceLeafStartup> world_leaf_startup;
    std::uint64_t source_water_material_write_revision{};
};
struct SoundHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
};
using SoundCache = std::unordered_map<std::string,std::shared_ptr<const audio::Clip>,SoundHash,std::equal_to<>>;
struct GameContext {
    assets::WorldScene& world;
    physics::PhysicsWorld& physics;
    audio::Audio& audio;
    SharedState& state;
    ComponentIds& ids;
    std::vector<VisualState>& visuals;
    const std::filesystem::path& asset_root;
    SoundCache& sounds;
    SourceEffects* source_effects{};
    const assets::ModelGroup& group(EntityId entity, std::uint32_t index=0) const;
    void animate(EntityId entity, std::string_view clip, Var percentage, bool cycle=false);
    void transition(EntityId entity, std::string_view clip, Var percentage, Var blend);
    audio::Handle sound(std::string_view source_name, Var volume=Var::literal(100), bool loop=false);
    void stop_music();
    std::shared_ptr<const audio::Clip> clip(std::string_view source_name) const;
    Var random(Var maximum);
    void copy_pose(EntityId to, EntityId from);
    void set_material(EntityId entity, std::string_view identity);
    void effect(std::string_view helper, Vec3 position, Vec3 normal={}, EntityId entity=no_entity, Var argument={});
};
Var source_random(SharedState& state, Var maximum);
// Adds source-created entities and binds/morphs actions BEFORE PhysicsWorld borrows world.
ComponentIds bootstrap_game(assets::WorldScene& world, const std::filesystem::path& asset_root,
                            SharedState& state, std::vector<VisualState>& visuals);
void initialize_game_physics(GameContext& context);
class GameRuntime {
public:
    GameRuntime(assets::WorldScene& world, physics::PhysicsWorld& physics, audio::Audio& audio,
                SharedState& state, ComponentIds& ids, std::vector<VisualState>& visuals,
                std::filesystem::path asset_root);
    ~GameRuntime();
    GameRuntime(const GameRuntime&)=delete;
    GameRuntime& operator=(const GameRuntime&)=delete;
    void advance_source_processes(const InputSnapshot& input, FrameTiming timing);
    void finish_source_frame(const InputSnapshot& input, const FrameTiming& timing);
    SourceViewSnapshot capture_draw_view() const;
    void publish_draw_frame();
    void dispatch_ui_command(UiCommand command, const InputSnapshot& input, const FrameTiming& timing);
    GameContext& context();
    std::string diagnostics() const;
    std::size_t source_world_queue_size() const;
    EntityId source_world_queued_entity(std::size_t ordinal) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace c4::game
