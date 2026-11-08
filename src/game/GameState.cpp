#include "GameState.hpp"
#include "VehicleController.hpp"
#include "RobotController.hpp"
#include "RaceCamera.hpp"
#include "CameraProjection.hpp"
#include "WorldActors.hpp"
#include "Ghost.hpp"
#include "audio/MotorAudio.hpp"
#include "effects/SourceEffects.hpp"
#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>
#include <pwd.h>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <unistd.h>

namespace c4::game {
using physics::v;
using physics::number;
const assets::ModelGroup& GameContext::group(EntityId entity, std::uint32_t index) const {
    const auto& binding=world.models.at(world.entities.at(entity).model_index);
    const auto& model=binding.morphed_model?binding.morphed_model:binding.model;
    if(!model)throw std::runtime_error("Source animation entity has no original model");
    return model->groups.at(index);
}
void GameContext::animate(EntityId entity,std::string_view name,Var percent,bool cycle) {
    auto& visual=visuals.at(entity);
    const auto sample=assets::animation_sample(group(entity),name,static_cast<float>(number(percent)),cycle);
    if(sample.first_frame!=visual.sample.first_frame||sample.second_frame!=visual.sample.second_frame||sample.blend!=visual.sample.blend)++visual.revision;
    visual.sample=sample;visual.frame=Var{sample.frame_raw};visual.next_frame=Var{sample.next_frame_raw};
}
void GameContext::transition(EntityId entity,std::string_view name,Var percent,Var blend) {
    auto& visual=visuals.at(entity);
    const auto sample=assets::animation_transition(group(entity),visual.sample,name,static_cast<float>(number(percent)),static_cast<float>(number(blend)));
    if(sample.first_frame!=visual.sample.first_frame||sample.second_frame!=visual.sample.second_frame||sample.blend!=visual.sample.blend)++visual.revision;
    visual.sample=sample;visual.frame=Var{sample.frame_raw};visual.next_frame=Var{sample.next_frame_raw};
}
void GameContext::set_material(EntityId entity,std::string_view identity) {
    auto& visual=visuals.at(entity);
    visual.material_identity=identity;
    visual.material_identity_known=true;
}
std::shared_ptr<const audio::Clip> GameContext::clip(std::string_view name) const {
    const auto found=sounds.find(name);
    if(found==sounds.end())throw std::runtime_error("Unbound original sound: "+std::string(name));
    return found->second;
}
audio::Handle GameContext::sound(std::string_view name,Var volume,bool loop) {
    return audio.play(clip(name),static_cast<float>(number(volume)),0,loop);
}
void GameContext::stop_music() {
    audio.stop(std::exchange(state.music_handle,0));
}
Var source_random(SharedState& state,Var maximum) {
    // Recovered SDK random()/CRT algorithm; other native engine consumers and order are unproven.
    state.rng_state=state.rng_state*214013u+2531011u;++state.rng_calls;
    const auto draw=(state.rng_state>>16)&32767u;
    const auto product=static_cast<std::int64_t>(draw)*maximum.raw;
    const auto rounded=(product>>10)+static_cast<std::int64_t>((static_cast<std::uint64_t>(product)>>9)&1);
    const auto wrapped=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(rounded));
    return Var{wrapped>>5};
}
Var GameContext::random(Var maximum) { return source_random(state,maximum); }
void GameContext::effect(std::string_view helper,Vec3 position,Vec3 normal,EntityId entity,Var argument) {
    if(state.effect_count==state.effects.size())throw std::length_error("Source effect request capacity exceeded");
    const EffectRequest request{helper,position,normal,entity,argument};
    state.effects[state.effect_count++]=request;
    // Controller-only tests may record requests; the concrete runtime always owns a sink.
    if(source_effects)source_effects->emit(*this,request);
}
void GameContext::copy_pose(EntityId to,EntityId from) {
    auto& target=physics.body(to);const auto& source=physics.body(from);
    target.position=source.position;target.angles=source.angles;
}
namespace {
std::string native_player_name() {
    // Original PLAYER_NAME starts empty; its nonempty defaults are networking-only.
    // Native policy: preserve the effective account name, or identify its numeric UID.
    const auto uid=::geteuid();
    passwd account{};
    passwd* found{};
    std::array<char,4096> buffer;
    if(::getpwuid_r(uid,&account,buffer.data(),buffer.size(),&found)==0&&found&&found->pw_name) {
        const std::string_view name{found->pw_name};
        // Ghost.cpp validate_track: no lossy folding, truncation, or Unicode conversion.
        if(!name.empty()&&name.size()<=128&&std::all_of(name.begin(),name.end(),
                [](unsigned char byte){return byte>=36&&byte<=125;}))
            return std::string(name);
    }
    // Missing/oversized passwd data and unrepresentable names retain a stable identity.
    return "uid-"+std::to_string(uid);
}
constexpr std::string_view sound_aliases[]{
    "nitro_start_wav","nitro_loop_wav",
    "carp_high1_wav","carp_high2_wav","carp_high3_wav","carp_high4_wav",
    "carp_low1_wav","carp_low2_wav","carp_low3_wav","carp_surt_wav","arac_dusme_wav",
    "robot_donus_wav","robot_donus_dus_wav","robot_raya_cik_wav","robot_raydan_in_wav",
    "robot_zipla_wav","robot_zipla_dus_wav","robot_adim_1_wav","robot_adim_2_wav","robot_adim_3_wav","robot_adim_4_wav",
    "robot_kay_asfalt_wav","robot_kay_demir_wav",
    "motor_ses_alev_ogg","motor_bas_cek_wav","motor_bosta_wav","motor_pati_wav","motor_kayma_wav","motor_geri_wav",
    "yol_kum_wav","yol_asfalt_wav",
    "ses_zaman_red","ses_zaman_green","ses_zaman_mekanik1","ses_zaman_mekanik2",
    "ses_checkpoint","ses_checkfinish",
    "ses_menu_carp","ses_buton_1","ses_buton_2","ses_buton_3","ses_buton_4",
    "buton_0_wav","buton_1_wav","buton_2_wav",
    "muzik_oyun_ses","muzik_menu_ses","load_music_wav",
    "helikopter_loop_ogg","sokak_ses_ogg","orman_ses_wav","deniz_ses_wav",
    "turboal_ses_wav"
};
const char* phase_name(GamePhase phase) {
    switch(phase){case GamePhase::menu:return "menu";case GamePhase::selection:return "selection";case GamePhase::intro:return "intro";case GamePhase::countdown:return "countdown";case GamePhase::race:return "race";case GamePhase::paused:return "paused";case GamePhase::results:return "results";}
    throw std::logic_error("Invalid game phase");
}
void source_restart(GameContext& c) {
    auto& s=c.state;
    ++s.history_epoch; // Actual source reset edge, including the original body teleport.
    s.collision_ok=v(0);s.crash_totaltime=v(100);s.crash_hedef=v(-666);
    s.c4_guncel_havazaman=v(0);s.c4_eski_havazaman=v(0);s.c4_havadongu_no=v(0);s.c4_dusme_dist=v(0);s.c4_yere_carpma=v(100);s.ghost_yaris_start=v(0);
    const ScriptVec spawn_angles{v(79.985),v(1.228),v(-.038)};
    const auto sdk_position=physics::native(ScriptVec{v(27685.279),v(-32348.588),v(-53.770)});
    const auto sdk_angles=physics::native(spawn_angles);
    for(auto id:{c.ids.p_aracgovde,c.ids.p_camera_target}){
        auto& body=c.physics.body(id);
        body.position=sdk_position;body.angles=sdk_angles;
    }
    s.camera_position={v(27646.008),v(-32570.992),v(-12.351)};s.camera_angles={v(79.980),v(.677),v(0)};
    s.arac_ivme=v(0);s.c4_leftright_yon=v(0);s.arac_gidis_pan=spawn_angles.x;s.arac_turbo=v(1);s.turbo_ekle_miktar=v(0);
    auto& robot_shadow=c.visuals.at(c.ids.p_robot_golge);robot_shadow.invisible=true;robot_shadow.alpha=v(0);robot_shadow.transparent=false;
    auto& car_shadow=c.visuals.at(c.ids.p_arac_golge);car_shadow.alpha=v(100);car_shadow.transparent=false;
    for(auto id:{c.ids.p_arac_cam,c.ids.p_far_govde,c.ids.p_far_cam,c.ids.p_arac_ontakim,c.ids.p_arac_koltuk,c.ids.p_arac_direx,c.ids.p_arac_kaporta,c.ids.p_arac_ayna,c.ids.p_arac_golge,c.ids.p_fsolteker,c.ids.p_fsagteker,c.ids.p_bsolteker,c.ids.p_bsagteker})c.visuals.at(id).invisible=false;
    for(auto id:{c.ids.p_arac_arkafar,c.ids.p_arac_arkaglow,c.ids.p_arac_tekerler,c.ids.p_robot_teker,c.ids.p_robot_mekanik,c.ids.p_robot_farcam,c.ids.p_robot_fargovde,c.ids.p_robot_camlar,c.ids.p_robot_kaporta})c.visuals.at(id).invisible=true;
    s.camera_tip_no=v(0);s.model_robot_ok=v(0);s.arac_durum_tip=v(0);s.phase_request=PhaseRequest::none;
}
}
struct GameRuntime::Impl {
    std::filesystem::path root;
    SoundCache sounds;
    SourceEffects effects;
    GameContext ctx;
    WorldActors world_actors;
    VehicleController car;
    RobotController robot;
    RaceCamera race;
    std::unique_ptr<audio::MotorAudio> motor;
    std::vector<float> offline_samples;
    std::uint32_t audio_frame_remainder{};
    int output_rate{};
    GamePhase resume_phase{GamePhase::race};
    bool launched{}, restart_wait{}, help_open{}, help_esc_seen{};
    std::uint64_t restart_frame{}, source_frames{}, paused_frames{};
    Var logical_ticks{};
    Var previous_reset{Var::literal(1)};
    enum class ReloadStage { inactive, bar, full_bar_wait, hidden_wait };
    ReloadStage reload_stage{ReloadStage::inactive};
    Var reload_percent{};
    std::uint64_t reload_frame{};
    struct RestartState {
        GamePhase phase;
        Var freeze, reset, menu, black_alpha;
        bool noparticle, bloom_noparticle, black_visible, help_visible, help_open, help_esc_seen;
    } restart_state{};
    Impl(assets::WorldScene& world,physics::PhysicsWorld& physics,audio::Audio& audio,SharedState& state,ComponentIds& ids,std::vector<VisualState>& visuals,std::filesystem::path asset_root)
        :root(std::move(asset_root)),effects(root),ctx{world,physics,audio,state,ids,visuals,root,sounds,&effects},world_actors(ctx) {
        state.oyuncu_ismi=native_player_name(); // game_start.wdl:48, before local Ghost load/HUD.
        if(audio.mode()==audio::Mode::Offline){
            output_rate=audio.output_rate();
            offline_samples.resize((static_cast<std::size_t>(output_rate)+31)/32*2);
        }
        const auto resolved=assets::resolve_resources(root/"C4.wrs",sound_aliases);
        std::unordered_map<std::filesystem::path,std::shared_ptr<const audio::Clip>> decoded;
        decoded.reserve(resolved.size());
        sounds.reserve(std::size(sound_aliases));
        for(std::size_t i=0;i<resolved.size();++i){
            auto [cached,inserted]=decoded.try_emplace(resolved[i]);
            if(inserted)cached->second=audio::load_clip(resolved[i]);
            sounds.emplace(std::string(sound_aliases[i]),cached->second);
        }
        motor=std::make_unique<audio::MotorAudio>(audio,audio::MotorClips{ctx.clip("motor_ses_alev_ogg"),ctx.clip("motor_bas_cek_wav"),ctx.clip("motor_bosta_wav"),ctx.clip("motor_pati_wav"),ctx.clip("motor_geri_wav"),ctx.clip("yol_kum_wav"),ctx.clip("yol_asfalt_wav")});
        initialize_game_physics(ctx);
        race.initialize(ctx);
        effects.initialize(ctx);
    }
    ~Impl(){world_actors.teardown(ctx);ctx.physics.on_entity={};}
    void advance_audio() {
        if(offline_samples.empty())return;
        audio_frame_remainder+=static_cast<std::uint32_t>(output_rate);
        const auto frames=audio_frame_remainder/32;audio_frame_remainder%=32;
        ctx.audio.render(std::span<float>{offline_samples.data(),static_cast<std::size_t>(frames)*2});
    }
    void launch() {
        launched=true;
        ctx.physics.on_entity=[this](const physics::EntityEvent& event){SourceMotion::bounce(ctx,event);};
    }
    void motor_tick(const InputSnapshot& input,Var time) {
        auto& s=ctx.state;
        if(s.oyunici_panel_acik!=v(1))return;
        const auto events=motor->update({s.arac_vites_no,s.arac_vites_ivme,s.motor_ses_ivme,s.arac_ivme,s.ghost_yaris_start!=v(0),s.toprak_yol_ok!=v(0),s.c4_eski_havazaman,input.key_cuu,input.key_space,s.arac_durum_tip,s.ghost_reset_ok!=v(0),s.freeze_mode,s.gameover_panel_acik!=v(0),time});
        const auto& state=motor->state();
        s.arac_pati_aktif=state.arac_pati_aktif?v(1):v(0);
        s.hud_vites_frame=state.loc_vites_frame;s.hud_motor_frame=state.loc_motor_frame;
        if(events.gear_flame_animation&&launched)car.gear_animation(ctx);
    }
    void pause(bool help) {
        auto& s=ctx.state;
        if(s.smart_menu_islem!=v(0))return;
        resume_phase=s.phase;s.smart_menu_islem=v(1);s.freeze_mode=v(1);s.phase=GamePhase::paused;
        if(!help){
            s.noparticle=true;s.bloom_noparticle=true;
            if(ctx.audio.playing(s.turbo_ses_handle2))ctx.audio.stop(s.turbo_ses_handle2);
            s.mouse_mode=v(2);
        }
        help_open=help;help_esc_seen=false;s.help_visible=help;
    }
    void resume() {
        auto& s=ctx.state;
        if(!help_open)s.mouse_mode=v(0);
        s.freeze_mode=v(0);if(!help_open){s.noparticle=false;s.bloom_noparticle=false;}
        s.smart_menu_islem=v(0);s.phase=resume_phase;help_open=false;s.help_visible=false;
    }
    void advance_reload(const FrameTiming& timing) {
        if(timing.frame_index<reload_frame)return;
        auto& s=ctx.state;
        switch(reload_stage){
        case ReloadStage::inactive:return;
        case ReloadStage::bar:
            s.loading_visible=true;s.loading_stage=4;
            if(reload_percent<=v(100)){
                s.loading_progress=reload_percent;
                reload_percent+=v(6.25)/v(2.5)*timing.originaltime_step_ticks;
            }else{
                s.loading_progress=reload_percent;reload_stage=ReloadStage::full_bar_wait;
            }
            break;
        case ReloadStage::full_bar_wait:
            // tekrarLevel_yukle:3032-3039, after the full-bar/fps wait.
            // Source var ses_sokakhandle is function-local, not the HUD ambience slot.
            (void)ctx.sound("sokak_ses_ogg",v(20),true);
            s.loading_visible=false;reload_stage=ReloadStage::hidden_wait;
            break;
        case ReloadStage::hidden_wait:
            // ana_menu.wdl:3040-3045, only after the hidden-panel wait(1).
            s.bloom_noshader=false;s.bloom_noparticle=false;
            s.noshader=false;s.noparticle=false;
            s.bloom_fx_ok=v(1);s.gamma_corrected_active=true; // bloom_acil.
            s.loading_stage=0;reload_stage=ReloadStage::inactive;
            race.reset(ctx); // c4_yaris_bitir enters its first wait; no early update this pass.
            break;
        }
        reload_frame=timing.frame_index+1;
    }
    void command(UiCommand ui_command,const InputSnapshot& input,const FrameTiming& timing) {
        auto& s=ctx.state;
        const auto frame=timing.frame_index;
        switch(ui_command){
        case UiCommand::none:break;
        case UiCommand::play:case UiCommand::select_local:case UiCommand::select_web:
            if(s.phase==GamePhase::menu||s.phase==GamePhase::selection){
                try{race.reset(ctx);s.record_error.clear();}
                catch(const GhostSaveError& error){s.record_error=error.what();break;}
                if(ui_command==UiCommand::select_local)s.selected_ghost=v(1);
                if(ui_command==UiCommand::select_web)s.selected_ghost=v(2);
                s.phase=GamePhase::intro;s.smart_menu_islem=v(1);s.freeze_mode=v(0);
                ctx.stop_music(); // Direct/resumed load-entry path also releases the menu loop.
                // First accepted Start loads bogazici; menu returns/restarts retain it (ana_menu.wdl:3268-3296).
                // Observed mode1 queues initial bodies; WorldActors enters them in the next eligible source pass.
                if(s.level1_yukleme_ok==v(0)){
                    s.ses_loadhandle=ctx.sound("load_music_wav",v(100),true);
                    s.level1_yukleme_ok=v(1);
                    for(std::size_t i=0;i<ctx.world.entities.size();++i){
                        const auto& entity=ctx.world.entities[i];
                        if(entity.object_index==no_entity||entity.native_prepared_inactive)continue;
                        auto& visual=ctx.visuals.at(i);
                        visual.material_identity=entity.source_initial_material_identity;
                        visual.material_identity_known=entity.source_initial_material_known;
                    }
                }
                else{
                    reload_stage=ReloadStage::bar;reload_percent=v(0);reload_frame=frame;
                    advance_reload(timing); // Direct tekrarLevel_yukle entry executes through its first wait.
                }
                world_actors.initialize(ctx,timing);
                if(reload_stage==ReloadStage::inactive)
                    race.update_early(ctx,input,timing); // First-load camera intro only.
            }break;
        case UiCommand::resume:if(s.phase==GamePhase::paused&&!help_open)resume();break;
        case UiCommand::close_help:if(s.help_visible)resume();break;
        case UiCommand::help:pause(true);break;
        case UiCommand::restart:
            if((s.phase==GamePhase::paused&&!help_open)||(s.phase==GamePhase::results&&s.finish_buttons_visible)){
                restart_state={s.phase,s.freeze_mode,s.ghost_reset_ok,s.smart_menu_islem,s.black_alpha,s.noparticle,s.bloom_noparticle,s.black_visible,s.help_visible,help_open,help_esc_seen};
                s.black_visible=true;s.black_alpha=v(100);s.ghost_reset_ok=v(1);s.freeze_mode=v(0);s.smart_menu_islem=v(1);
                ctx.stop_music(); // Resumed callback body; physical UI already stopped after its fade.
                previous_reset=v(1);effects.clear();
                restart_wait=true;restart_frame=frame+1;help_open=false;s.help_visible=false;
            }break;
        case UiCommand::menu:
            if((s.phase==GamePhase::paused&&!help_open)||(s.phase==GamePhase::results&&s.finish_buttons_visible)){
                s.phase=GamePhase::menu;s.freeze_mode=v(0);s.smart_menu_islem=v(1);s.ghost_reset_ok=v(1);s.ghost_yaris_start=v(0);
                s.gamma_corrected_active=false; // Actual source bloom_kapan on menu-return callbacks.
                previous_reset=v(1);effects.clear();
                ctx.stop_music();ctx.audio.stop(std::exchange(s.ses_sokakhandle,0));
                restart_wait=false;help_open=false;s.help_visible=false;
            }break;
        case UiCommand::quit:
            if(s.phase==GamePhase::menu||s.phase==GamePhase::selection){s.quit_requested=true;ctx.audio.stop_all();}
            break;
        }
    }
    void advance_late_source(const InputSnapshot& input,const FrameTiming& timing) {
        world_actors.update(ctx,timing);
        effects.run_processes(ctx,timing);
        if(help_open){if(input.key_esc)help_esc_seen=true;else if(help_esc_seen)resume();}
    }
    void finish_source_frame(const InputSnapshot& input,const FrameTiming& timing) {
        // Original core runs its process passes before ascending keyboard callbacks.
        if(input.key_esc_pressed)pause(false);
        if(input.key_f1_pressed){
            pause(true);
            if(help_open&&input.key_esc)help_esc_seen=true;
        }
        effects.update(ctx,timing);
        advance_audio();
    }
    void advance_source_processes(const InputSnapshot& input,FrameTiming timing) {
        if(timing.originaltime_step_ticks!=v(.5))throw std::invalid_argument("Native source runtime requires one half-tick per update");
        auto& s=ctx.state;
        s.effect_count=0;
        if(restart_wait&&timing.frame_index>=restart_frame){
            restart_wait=false;
            try{race.reset(ctx);s.record_error.clear();}
            catch(const GhostSaveError& error){
                s.record_error=error.what();s.phase=restart_state.phase;s.freeze_mode=restart_state.freeze;s.ghost_reset_ok=restart_state.reset;
                s.smart_menu_islem=restart_state.menu;s.black_alpha=restart_state.black_alpha;s.noparticle=restart_state.noparticle;s.bloom_noparticle=restart_state.bloom_noparticle;
                s.black_visible=restart_state.black_visible;s.help_visible=restart_state.help_visible;
                help_open=restart_state.help_open;help_esc_seen=restart_state.help_esc_seen;previous_reset=s.ghost_reset_ok;
            }
        }
        robot.update_auxiliary(ctx,timing);
        if(s.phase==GamePhase::menu||s.phase==GamePhase::selection||s.quit_requested){
            previous_reset=s.ghost_reset_ok;
            motor_tick(input,timing.originaltime_step_ticks);advance_late_source(input,timing);return;
        }
        if(s.freeze_mode){
            ++paused_frames;previous_reset=s.ghost_reset_ok;
            motor_tick(input,timing.originaltime_step_ticks);advance_late_source(input,timing);return;
        }
        logical_ticks+=timing.originaltime_step_ticks;timing.seconds_deadlines=number(logical_ticks/v(16));
        if(reload_stage!=ReloadStage::inactive){
            advance_reload(timing);
            motor_tick(input,timing.originaltime_step_ticks);advance_late_source(input,timing);
            ++source_frames;
            return;
        }
        race.update_early(ctx,input,timing);
        if(previous_reset==v(1)&&s.ghost_reset_ok==v(0)){
            source_restart(ctx);robot.reset(ctx);car.reset(ctx);
            if(!launched)launch();
        }
        previous_reset=s.ghost_reset_ok;
        motor_tick(input,timing.originaltime_step_ticks);
        if(launched&&s.ghost_reset_ok==v(0)){
            const bool robot_owns_motion=robot.phase()!=RobotController::Phase::inactive;
            car.update(ctx,input,timing,true,!robot_owns_motion);
            if(s.phase_request==PhaseRequest::robot){s.phase_request=PhaseRequest::none;robot.initialize(ctx,timing);robot.update(ctx,input,timing);}
            else if(robot_owns_motion)robot.update(ctx,input,timing);
            if(s.phase_request==PhaseRequest::car){
                s.phase_request=PhaseRequest::none;
                car.start_settling(ctx,s.car_settling_frame,s.car_settling_speed);
                car.initialize(ctx);car.update(ctx,input,timing,false);
                ctx.sound("robot_donus_dus_wav");
            }
        }
        race.update_late(ctx,input,timing);
        advance_late_source(input,timing);
        ++source_frames;
    }
};
GameRuntime::GameRuntime(assets::WorldScene& w,physics::PhysicsWorld& p,audio::Audio& a,SharedState& s,ComponentIds& ids,std::vector<VisualState>& v,std::filesystem::path root)
    :impl_(std::make_unique<Impl>(w,p,a,s,ids,v,std::move(root))){}
GameRuntime::~GameRuntime()=default;
void GameRuntime::advance_source_processes(const InputSnapshot& input,FrameTiming timing){impl_->advance_source_processes(input,timing);}
void GameRuntime::finish_source_frame(const InputSnapshot& input,const FrameTiming& timing){impl_->finish_source_frame(input,timing);}
SourceViewSnapshot GameRuntime::capture_draw_view() const{return capture_source_view(impl_->ctx.state);}
void GameRuntime::publish_draw_frame(){impl_->effects.publish_frame(impl_->ctx);}
void GameRuntime::dispatch_ui_command(UiCommand command,const InputSnapshot& input,const FrameTiming& timing){impl_->command(command,input,timing);}
GameContext& GameRuntime::context(){return impl_->ctx;}
std::size_t GameRuntime::source_world_queue_size() const{return impl_->world_actors.coverage().source_queue_size;}
EntityId GameRuntime::source_world_queued_entity(std::size_t ordinal) const{return impl_->world_actors.queued_entity(ordinal);}
std::string GameRuntime::diagnostics() const {
    const auto& c=impl_->ctx;const auto& s=c.state;const auto& body=c.physics.body(c.ids.p_aracgovde);const auto& target=c.physics.body(c.ids.p_camera_target);
    const auto bound_paths=std::count_if(c.ids.path_ent_dizi.begin(),c.ids.path_ent_dizi.end(),[&](EntityId id){return id!=no_entity&&id<c.world.entities.size();});
    const auto original_entities=std::count_if(c.world.entities.begin(),c.world.entities.end(),[](const assets::WorldEntity& entity){return entity.object_index!=no_entity;});
    const auto prepared_leaves=std::count_if(c.world.entities.begin(),c.world.entities.end(),[](const assets::WorldEntity& entity){return entity.object_index==no_entity&&entity.action=="tl1_LeaveInit";});
    const auto actors=impl_->world_actors.coverage();
    std::ostringstream out;out<<std::setprecision(17)<<"{\"game_mode\":\"native-source-port\",\"numeric_parity\":\"not-proven\",\"scheduler_parity\":\"not-proven\",\"phase\":\""<<phase_name(s.phase)<<"\",\"source_frames\":"<<impl_->source_frames<<",\"paused_frames\":"<<impl_->paused_frames<<",\"prepared_world_entities\":"<<c.world.entities.size()<<",\"bound_paths\":"<<bound_paths<<",\"bound_pickups\":"<<c.ids.pickups.size()<<",\"vehicle_controller_launched\":"<<(impl_->launched?"true":"false")<<",\"source_ticks\":"<<number(impl_->logical_ticks);
    out<<",\"prepared_original_world_entities\":"<<original_entities<<",\"prepared_helper_entities\":"<<c.world.entities.size()-static_cast<std::size_t>(original_entities);
    out<<",\"prepared_leaf_entities\":"<<prepared_leaves<<",\"source_loaded_world_entities\":"<<(actors.queued?original_entities:0)
       <<",\"source_born_leaf_entities\":"<<actors.leaves.born<<",\"source_initialized_leaf_entities\":"<<actors.leaves.initialized;
    const auto& profile=c.world.source_profile;
    out<<",\"source_profile\":{\"atq\":"<<static_cast<unsigned>(profile.atq)<<",\"mtq\":"<<static_cast<unsigned>(profile.mtq)
       <<",\"stq\":"<<static_cast<unsigned>(profile.stq)<<",\"cgm\":"<<static_cast<unsigned>(profile.cgm)<<",\"shq\":"<<static_cast<unsigned>(profile.shq)
       <<",\"ano\":"<<static_cast<unsigned>(profile.ano)<<",\"bloom\":"<<static_cast<unsigned>(profile.bloom)<<'}';
    out<<",\"particle_source_count\":{\"value\":"<<number(s.particle_count)<<",\"raw\":"<<s.particle_count.raw<<'}'
       <<",\"camera_far_source\":{\"cgm\":"<<static_cast<unsigned>(profile.cgm)<<",\"clip_far\":"<<number(s.camera_clip_far)<<",\"clip_far_raw\":"<<s.camera_clip_far.raw<<'}';
    out<<",\"runtime_phase_order\":\"source-processes,source-ui-continuation,keyboard,particles,audio,ui1,frame-publication,ui2\"";
    out<<",\"keyboard_callback_scope\":\"recovered-press-latch-order\",\"audio_mode\":\""<<(c.audio.mode()==audio::Mode::Offline?"offline-real-mixer":"device")
       <<"\",\"audio_output_rate\":"<<c.audio.output_rate()<<",\"audio_rendered_frames\":"<<c.audio.rendered_frames();
    auto text=[&](std::string_view value){
        constexpr char hex[]="0123456789abcdef";out<<'"';
        for(unsigned char ch:value){
            if(ch=='"'||ch=='\\')out<<'\\'<<static_cast<char>(ch);
            else if(ch<32)out<<"\\u00"<<hex[ch>>4]<<hex[ch&15];
            else out<<static_cast<char>(ch);
        }
        out<<'"';
    };
    out<<",\"world_source_actions\":{\"queued\":"<<(actors.queued?"true":"false")<<",\"started\":"<<(actors.started?"true":"false")
       <<",\"pending_entries\":"<<actors.pending_entries<<",\"source_queue_size\":"<<actors.source_queue_size<<",\"first_queued_id\":"<<actors.first_queued_id
       <<",\"cycles\":"<<actors.cycles<<",\"active_cycles\":"<<actors.active_cycles
       <<",\"facing\":"<<actors.facing<<",\"active_facing\":"<<actors.active_facing
       <<",\"skins\":"<<actors.skins<<",\"active_skins\":"<<actors.active_skins
       <<",\"banners\":"<<actors.banners<<",\"active_banners\":"<<actors.active_banners
       <<",\"water\":"<<actors.water
       <<",\"helicopters\":"<<actors.paths.helicopters<<",\"active_helicopters\":"<<actors.paths.active_helicopters
       <<",\"trios\":"<<actors.paths.trios<<",\"active_trios\":"<<actors.paths.active_trios
       <<",\"schools\":"<<actors.paths.schools<<",\"active_schools\":"<<actors.paths.active_schools
       <<",\"gated_entities\":"<<actors.paths.gated_entities
       <<",\"leaves\":{\"queued\":"<<(actors.leaves.queued?"true":"false")<<",\"trees\":"<<actors.leaves.trees<<",\"prepared\":"<<actors.leaves.prepared
       <<",\"pending_trees\":"<<actors.leaves.pending_trees<<",\"born\":"<<actors.leaves.born<<",\"initialized\":"<<actors.leaves.initialized
       <<",\"pending_callbacks\":"<<actors.leaves.pending_callbacks<<'}'
       <<",\"first_cycle\":{\"entity_id\":"<<actors.cycle_id<<",\"frame\":"<<number(actors.cycle_frame)<<",\"frame_raw\":"<<actors.cycle_frame.raw<<'}'
       <<",\"first_skin\":{\"entity_id\":"<<actors.skin_id<<",\"frame\":"<<number(actors.skin_frame)<<",\"frame_raw\":"<<actors.skin_frame.raw<<",\"skin\":"<<actors.skin_number<<'}'
       <<",\"first_banner\":{\"entity_id\":"<<actors.banner_id<<",\"frame\":"<<number(actors.banner_frame)<<",\"frame_raw\":"<<actors.banner_frame.raw
       <<",\"wait\":"<<number(actors.banner_wait)<<",\"wait_raw\":"<<actors.banner_wait.raw<<",\"reverse\":"<<(actors.banner_reverse?"true":"false")<<"}}";
    out<<",\"world_action_queue\":[";
    for(std::size_t i=0;i<actors.source_queue_size;++i){
        if(i)out<<',';out<<impl_->world_actors.queued_entity(i);
    }
    out<<']';
    out<<",\"world_action_start_scope\":\"original-mode1-queued-initial\""
       <<",\"world_action_order_scope\":\"cross-family/load-progress-order-unproven\"";
    out<<",\"world_action_source_owners\":[";
    for(std::size_t i=0;i<world_actor_source_owners.size();++i){
        if(i)out<<',';
        const auto& owner=world_actor_source_owners[i];
        out<<"{\"action\":";text(owner.action);out<<",\"native_owner\":";text(owner.native_owner);
        out<<",\"source_file\":";text(owner.action=="water_yeni"?"effect_define.wdl":"c4_effects.wdl");
        out<<",\"source_begin\":"<<owner.source_begin<<",\"source_end\":"<<owner.source_end<<'}';
    }
    out<<']';
    const std::pair<std::string_view,EntityId> bindings[]{
        {"p_aracgovde",c.ids.p_aracgovde},{"p_camera_target",c.ids.p_camera_target},
        {"p_arac_cam",c.ids.p_arac_cam},{"p_far_govde",c.ids.p_far_govde},{"p_far_cam",c.ids.p_far_cam},
        {"p_arac_ontakim",c.ids.p_arac_ontakim},{"p_arac_koltuk",c.ids.p_arac_koltuk},{"p_arac_kaporta",c.ids.p_arac_kaporta},
        {"p_arac_direx",c.ids.p_arac_direx},{"p_arac_ayna",c.ids.p_arac_ayna},{"p_arac_tekerler",c.ids.p_arac_tekerler},
        {"p_arac_arkafar",c.ids.p_arac_arkafar},{"p_arac_arkaglow",c.ids.p_arac_arkaglow},
        {"p_fsolteker",c.ids.p_fsolteker},{"p_fsagteker",c.ids.p_fsagteker},{"p_bsolteker",c.ids.p_bsolteker},{"p_bsagteker",c.ids.p_bsagteker},{"p_arac_golge",c.ids.p_arac_golge},
        {"p_ic_camlar",c.ids.p_ic_camlar},{"p_ic_turbo",c.ids.p_ic_turbo},{"p_ic_panel",c.ids.p_ic_panel},{"p_ic_tavan",c.ids.p_ic_tavan},
        {"p_ic_simit",c.ids.p_ic_simit},{"p_ic_rakam0",c.ids.p_ic_rakam0},{"p_ic_rakam1",c.ids.p_ic_rakam1},{"p_ic_rakam2",c.ids.p_ic_rakam2},
        {"p_robot_teker",c.ids.p_robot_teker},{"p_robot_mekanik",c.ids.p_robot_mekanik},{"p_robot_farcam",c.ids.p_robot_farcam},
        {"p_robot_fargovde",c.ids.p_robot_fargovde},{"p_robot_camlar",c.ids.p_robot_camlar},{"p_robot_kaporta",c.ids.p_robot_kaporta},{"p_robot_golge",c.ids.p_robot_golge},
        {"p_ghost_arac",c.ids.p_ghost_arac},{"p_ghost_robo",c.ids.p_ghost_robo},{"p_zamanlama",c.ids.p_zamanlama}
    };
    out<<",\"component_bindings\":{";
    for(std::size_t i=0;i<std::size(bindings);++i){
        if(i)out<<',';text(bindings[i].first);const auto id=bindings[i].second;out<<":{\"entity_id\":"<<id;
        if(id!=no_entity&&id<c.world.entities.size()){
            const auto& entity=c.world.entities[id];
            out<<",\"object_index\":"<<entity.object_index<<",\"model_index\":"<<entity.model_index<<",\"source_resource\":";text(entity.resource);
            out<<",\"source_action\":";text(entity.action);
            if(entity.model_index<c.world.models.size()){
                const auto& model=c.world.models[entity.model_index];out<<",\"model_resource\":";text(model.resource.generic_string());
                out<<",\"morphed_resource\":";text(model.morphed_resource.generic_string());
            }
        }
        out<<'}';
    }
    out<<"},\"pickup_entities\":[";
    for(std::size_t i=0;i<c.ids.pickups.size();++i){if(i)out<<',';out<<c.ids.pickups[i];}
    out<<"],\"inspection_unresolved_actions\":[";
    for(std::size_t i=0;i<c.world.unresolved_actions.size();++i){if(i)out<<',';text(c.world.unresolved_actions[i]);}
    out<<']';
    out<<",\"record_error\":";text(s.record_error);
    out<<",\"effects_diagnostics\":";text(impl_->effects.diagnostics());
    auto vector=[&](const char* name,const Vec3& value){out<<",\""<<name<<"\":["<<value.x<<','<<value.y<<','<<value.z<<']';};
    auto source_vector=[&](const char* name,const ScriptVec& value){out<<",\""<<name<<"\":["<<number(value.x)<<','<<number(value.y)<<','<<number(value.z)<<']';};
    vector("body_position",body.position);vector("body_angles",body.angles);vector("camera_target_position",target.position);
    source_vector("camera_position",s.camera_position);source_vector("camera_angles",s.camera_angles);
    source_vector("bloom_camera_position",s.bloom_camera_position);source_vector("bloom_camera_angles",s.bloom_camera_angles);
    const std::pair<std::string_view,Var> scalars[]{
        {"source_ticks",impl_->logical_ticks},{"arac_ivme",s.arac_ivme},{"arac_vites_no",s.arac_vites_no},
        {"turbo_ekle_miktar",s.turbo_ekle_miktar},{"model_robot_ok",s.model_robot_ok},{"arac_durum_tip",s.arac_durum_tip},
        {"robot_percent",impl_->robot.percentage()},{"camera_tipno",s.camera_tipno},{"oyun_oynanabilir",s.oyun_oynanabilir},
        {"ghost_reset_ok",s.ghost_reset_ok},{"ghost_yaris_start",s.ghost_yaris_start},{"freeze_mode",s.freeze_mode},
        {"checkpoint_no",s.checkpoint_no},{"t_oyun_zaman",s.t_oyun_zaman},{"game_over_ok",s.game_over_ok},
        {"gameover_panel_acik",s.gameover_panel_acik},{"hud_vites_frame",s.hud_vites_frame},{"hud_motor_frame",s.hud_motor_frame},
        {"loading_progress",s.loading_progress},{"selected_ghost",s.selected_ghost},
        {"arac_max_hiz",s.arac_max_hiz},{"arac_gidis_pan",s.arac_gidis_pan},{"arac_gidis_tilt",s.arac_gidis_tilt},
        {"arac_turbo",s.arac_turbo},{"start_turbo",s.start_turbo},{"c4_leftright_yon",s.c4_leftright_yon},
        {"arac_vites_ivme",s.arac_vites_ivme},{"motor_ses_ivme",s.motor_ses_ivme},{"motor_ses_volume",s.motor_ses_volume},
        {"c4_vites1_time",s.c4_vites1_time},{"c4_vites7_time",s.c4_vites7_time},{"arac_pati_aktif",s.arac_pati_aktif},
        {"yercekimi_gucu",s.yercekimi_gucu},{"yc_alt_mesafe",s.yc_alt_mesafe},{"c4_guncel_havazaman",s.c4_guncel_havazaman},
        {"c4_eski_havazaman",s.c4_eski_havazaman},{"c4_havadongu_no",s.c4_havadongu_no},{"c4_dusme_dist",s.c4_dusme_dist},{"c4_yere_carpma",s.c4_yere_carpma},
        {"toprak_yol_ok",s.toprak_yol_ok},{"collision_ok",s.collision_ok},{"crash_hedef",s.crash_hedef},{"crash_miktar",s.crash_miktar},{"crash_totaltime",s.crash_totaltime},
        {"body_skill2",s.body_skill2},{"robot_action_no",s.robot_action_no},{"robot_frame_no",s.robot_frame_no},{"temp_ghostframe",s.temp_ghostframe},{"r_kayma_fark",s.r_kayma_fark},
        {"cam_path_pan",s.cam_path_pan},{"cam_path_tilt",s.cam_path_tilt},{"cam_path_up",s.cam_path_up},{"cam_path_xy",s.cam_path_xy},{"cam_path_dist",s.cam_path_dist},{"cam_path_ivme",s.cam_path_ivme},
        {"camera_tip_no",s.camera_tip_no},{"camrec_regno",s.camrec_regno},{"checkpoint_oldframe",s.checkpoint_oldframe},{"t_oyun_bonus",s.t_oyun_bonus},{"race_lap",s.race_lap},
        {"level_restart_no",s.level_restart_no},{"smart_menu_islem",s.smart_menu_islem},{"oyunici_panel_acik",s.oyunici_panel_acik},{"min_ghost_dist",s.min_ghost_dist},{"black_alpha",s.black_alpha},
        {"ghost_rekor",s.ghost_rekor},{"local_ghost_ticks",s.local_ghost_ticks},{"checkpoint_delta_ticks",s.checkpoint_delta_ticks},{"gauge_needle_roll",s.gauge_needle_roll},
        {"particle_count",s.particle_count},{"camera_arc",s.camera_arc},{"camera_clip_near",s.camera_clip_near},{"camera_clip_far",s.camera_clip_far},
        {"bloom_camera_arc",s.bloom_camera_arc},{"bloom_aktif",s.bloom_aktif},{"bloom_fx_ok",s.bloom_fx_ok},
        {"mouse_mode",s.mouse_mode},{"d3d_anisotropy",s.d3d_anisotropy}
    };
    for(const auto& [name,value]:scalars)if(name!="source_ticks")out<<",\""<<name<<"\":"<<number(value);
    out<<",\"hud_speed\":"<<static_cast<int>(number(physics::var_abs(s.arac_ivme)*v(.8)))<<",\"robot_phase\":"<<static_cast<int>(impl_->robot.phase())<<",\"gamma_corrected_active\":"<<(s.gamma_corrected_active?"true":"false")
       <<",\"help_visible\":"<<(s.help_visible?"true":"false")<<",\"loading_visible\":"<<(s.loading_visible?"true":"false")<<",\"loading_stage\":"<<s.loading_stage
       <<",\"body_entity\":"<<c.ids.p_aracgovde<<",\"camera_target_entity\":"<<c.ids.p_camera_target<<",\"selected_path_entity\":"<<s.selected_path<<",\"path_entities\":[";
    for(std::size_t i=0;i<c.ids.path_ent_dizi.size();++i){if(i)out<<',';out<<c.ids.path_ent_dizi[i];}
    out<<"],\"source_raw\":{";
    for(std::size_t i=0;i<std::size(scalars);++i){if(i)out<<',';out<<'\"'<<scalars[i].first<<"\":"<<scalars[i].second.raw;}
    out<<"},\"source_vector_raw\":{";
    auto raw_vector=[&](std::string_view name,const ScriptVec& value){text(name);out<<":["<<value.x.raw<<','<<value.y.raw<<','<<value.z.raw<<']';};
    raw_vector("arac_cekimkuvveti",s.arac_cekimkuvveti);out<<',';raw_vector("crash_angle",s.crash_angle);out<<',';raw_vector("crash_target",s.crash_target);
    out<<',';raw_vector("camera_position",s.camera_position);out<<',';raw_vector("camera_angles",s.camera_angles);
    out<<',';raw_vector("bloom_camera_position",s.bloom_camera_position);out<<',';raw_vector("bloom_camera_angles",s.bloom_camera_angles);
    out<<"},\"source_array_raw\":{";
    auto raw_array=[&](std::string_view name,const auto& values){text(name);out<<":[";for(std::size_t i=0;i<values.size();++i){if(i)out<<',';out<<values[i].raw;}out<<']';};
    raw_array("path_dist_diz",s.path_dist_diz);out<<',';raw_array("path_top_no",s.path_top_no);out<<',';
    raw_array("t_point_zaman",s.t_point_zaman);out<<',';raw_array("ghost_point_time",s.ghost_point_time);
    out<<"},\"effect_requests\":"<<s.effect_count<<",\"effect_particles\":"<<impl_->effects.frame().counts.active
       <<",\"effects_emitted\":"<<impl_->effects.frame().counts.emitted<<",\"effects_expired\":"<<impl_->effects.frame().counts.expired
       <<",\"effects_allocation_failures\":"<<impl_->effects.frame().counts.allocation_failures<<",\"effects_recycled\":"<<impl_->effects.frame().counts.recycled
       <<",\"rng_scope\":\"original-sdk-algorithm-port\",\"rng_order_scope\":\"native-enginecall-scope/order-unproven\",\"rng_state\":"<<s.rng_state<<",\"rng_calls\":"<<s.rng_calls<<'}';
    return out.str();
}
} // namespace c4::game
