#include "RaceCamera.hpp"
#include "Ghost.hpp"
#include "RaceLifecycle.hpp"
#include "SourceTime.hpp"
#include <sstream>
#include <stdexcept>

namespace c4::game {
namespace {
using physics::Var;
using physics::ScriptVec;
using physics::v;
using physics::number;
using physics::body_source;
using physics::native;
using physics::var_abs;
using physics::var_sign;
using physics::var_min;
using physics::var_max;
using physics::var_clamp;
using physics::var_integer;
using physics::var_modulo;
using physics::var_ang;
using physics::var_sin;
using physics::var_cos;
using physics::var_sqrt;
using physics::var_vec_scale;
using physics::var_vec_rotate;
using physics::var_vec_to_angle;
using physics::var_vec_dist;
Var smooth(Var old,Var target,Var gain,Var time,bool angular=false,bool snap=false) {
    const auto delta=angular?var_ang(target-old):target-old;
    const auto value=old+gain*delta*v(.5)*var_sqrt(v(2)*time);
    const auto remaining=angular?var_ang(target-value):target-value;
    return snap||var_sign(delta)!=var_sign(remaining)?target:value;
}
const std::array<std::array<Var,4>,5> gates{{
    {v(16811.617),v(52681.492),v(16126.169),v(53152.059)},
    {v(-19825.926),v(45132.727),v(-19085.248),v(45219.867)},
    {v(-43974.715),v(-30874.541),v(-45091.395),v(-31609.875)},
    {v(-6221.184),v(-22568.174),v(-6782.038),v(-22119.979)},
    {v(27881.670),v(-32164.383),v(27565.885),v(-32102.354)}}};
constexpr std::array<int,100> shake_table{68,11,12,0,22,81,4,8,9,3,43,20,62,26,74,83,67,19,51,21,44,23,88,57,56,27,87,58,76,94,6,33,35,34,50,84,17,69,85,66,71,79,90,89,37,47,78,7,53,59,15,18,60,70,65,2,95,1,36,49,13,54,92,97,24,86,5,52,91,96,72,64,75,75,30,77,61,55,63,14,82,16,10,41,41,39,46,38,93,45,99,31,29,42,25,73,98,28,48};
constexpr std::array<std::string_view,12> actions{"donus","kosma","arac_ol","hopsag_up","hopsag_kay","hopsag_in","tutma_up","tutma_loop","tutma_in","hopsol_up","hopsol_kay","hopsol_in"};
void visible(GameContext& c,EntityId id,bool show) { if(id!=no_entity) { auto& v=c.visuals.at(id); v.invisible=!show; ++v.revision; } }
void publish_bloom(SharedState& s) { if(s.bloom_aktif) { s.bloom_camera_position=s.camera_position;s.bloom_camera_angles=s.camera_angles;s.bloom_camera_arc=s.camera_arc; } }
GhostRecord record(GameContext& c,Var ticks,bool initial=false) {
    const auto& b=c.physics.body(c.ids.p_camera_target);
    auto angles=body_source(b.angles);
    angles.x=var_modulo(angles.x,v(360));angles.y=var_modulo(angles.y,v(360));angles.z=var_modulo(angles.z,v(360));
    const auto& s=c.state;
    return {ticks,body_source(b.position),angles,initial||s.arac_durum_tip?s.robot_action_no.raw/1024:0,
            initial||s.arac_durum_tip?s.robot_frame_no:Var{},s.arac_durum_tip.raw/1024};
}
} // namespace
GateObservation observe_race_gate(GameContext& c,int gate) {
    if(gate<0||gate>=10) throw std::out_of_range("Source checkpoint index outside ten gates");
    const auto& g=gates[gate%5];
    const ScriptVec a{g[0],g[1],Var{}},b{g[2],g[3],Var{}};
    auto p=body_source(c.physics.body(c.ids.p_camera_target).position);p.z=Var{};
    const auto heading=var_vec_to_angle(b-a).x;
    auto side_angles=var_vec_to_angle(p-a);
    const auto opposite_angles=var_vec_to_angle(p-b);
    GateObservation result{
        var_abs(var_ang(side_angles.x-heading))<v(110)&&
        var_abs(var_ang(opposite_angles.x-heading-v(180)))<v(110)&&
        var_vec_dist(var_vec_scale(p,v(.001)),var_vec_scale(a,v(.001)))<v(1.5),0};
    if(result.eligible) for(std::uint32_t vertex:{4u,3u,2u,1u}) {
        auto point=body_source(c.physics.vec_for_vertex(c.ids.p_camera_target,vertex,c.visuals.at(c.ids.p_camera_target).sample));point.z=Var{};
        side_angles=var_vec_to_angle(point-a,side_angles);
        result.side_sum+=var_sign(var_ang(side_angles.x-heading)).raw/1024;
    }
    return result;
}
struct RaceCamera::Impl {
    CountdownPickups lifecycle;
    RaceProgress progress;
    GhostTrack recording;
    std::optional<GhostTrack> local;
    physics::Var clock{},last_sample{},intro_angle{},intro_pan{};
    bool intro_started{},camera_started{},camera_done{},record_started{},capacity_exceeded{},archive_pending{};
    std::uint64_t camera_start_frame{},finish_frame{};
    physics::Var finish_fade{};
    double finish_deadline{};
    int finish_stage{};
    Var orbit{},pan{},pitch{v(12.69)},height{v(44.09)},target_pitch{v(12.69)},far{},turbo{v(1)},robot_dist{},robot_arc{},old_speed{},squash{},shake_phase{},shake_amount{},shake_duration{v(.5)};
    ScriptVec camera_direction{};
    Var mode_seen{v(-1)},robot_seen{},shake_index{};
    int intro_stage{};
    std::uint64_t intro_done_frame{};
    bool c_latch{},robot_camera{};
    void mode(GameContext& c,int mode,bool restore=false) {
        auto& s=c.state;const auto& i=c.ids;
        if(mode_seen!=v(mode)) ++s.history_epoch;
        const std::array inside{i.p_ic_camlar,i.p_ic_panel,i.p_ic_tavan,i.p_ic_simit,i.p_ic_rakam0,i.p_ic_rakam1,i.p_ic_rakam2,i.p_ic_turbo};
        const std::array outside{i.p_arac_golge,i.p_arac_cam,i.p_far_govde,i.p_far_cam,i.p_arac_ontakim,i.p_fsolteker,i.p_fsagteker,i.p_bsolteker,i.p_bsagteker,i.p_arac_koltuk,i.p_arac_direx,i.p_arac_kaporta,i.p_arac_ayna};
        if(mode==0) { for(auto id:inside)visible(c,id,false); if(!s.model_robot_ok)for(auto id:outside)visible(c,id,true);s.hud_speed_visible=true; }
        if(mode==2) { for(auto id:inside)visible(c,id,true);for(auto id:outside)visible(c,id,false);visible(c,i.p_arac_tekerler,false);far=Var{};s.hud_speed_visible=false;camera_direction=body_source(c.physics.body(i.p_aracgovde).angles);camera_direction.y-=v(8.959); }
        if(mode==3) { for(auto id:inside)visible(c,id,false);if(restore)for(auto id:outside)visible(c,id,false);s.hud_speed_visible=true; }
        s.noparticle=s.bloom_noparticle=mode==2;s.min_ghost_dist=mode==2?v(230):mode==3?v(200):v(100);mode_seen=v(mode);
    }
    void begin_camera(GameContext& c,std::uint64_t frame) {
        auto& s=c.state;const auto position=body_source(c.physics.body(c.ids.p_aracgovde).position);
        ++s.history_epoch; // New chase coroutine selects its initial source VIEW pose.
        // cam_ang.pan and c_farcam_oran are globals, unlike the reset coroutine locals.
        orbit=Var{};pitch=v(12.69);height=v(44.09);target_pitch=v(12.69);turbo=v(1);
        robot_dist=robot_arc=old_speed=squash=Var{};shake_index=robot_seen=Var{};
        robot_camera=false;c_latch=false;camera_done=false;camera_started=true;camera_start_frame=frame;
        s.camera_arc=v(60);
        const ScriptVec camera_position{
            position.x+var_cos(pan)*v(232.96)*var_cos(pitch),
            position.y+var_sin(pan)*v(232.96)*var_cos(pitch),
            position.z+var_sin(pitch)*v(232.96)};
        s.camera_position=camera_position;
        s.camera_angles=var_vec_to_angle(position+ScriptVec{Var{},Var{},v(40)}-camera_position,s.camera_angles);
        camera_direction={};s.r_kayma_fark=Var{};mode_seen=v(-1);publish_bloom(s);
    }
    void camera(GameContext& c,const InputSnapshot& input,const FrameTiming& timing) {
        auto& s=c.state;if(camera_done)return;if(!camera_started)begin_camera(c,timing.frame_index);
        if(timing.frame_index==camera_start_frame)return;
        const auto& b=c.physics.body(c.ids.p_aracgovde);const auto& target=c.physics.body(c.ids.p_camera_target);
        const auto body_position=body_source(b.position),body_angles=body_source(b.angles);
        const auto target_position=body_source(target.position),target_angles=body_source(target.angles);
        const auto t=timing.originaltime_step_ticks;const bool snap=!s.ghost_yaris_start;
        if(mode_seen<v(0)) {
            pan=target_angles.x-v(180);
            // camera_ingame_fx clears both VIEW particle gates after its first wait.
            s.noparticle=s.bloom_noparticle=false;
            if(!s.camrec_regno){if(s.camera_tipno==v(3))mode(c,2);mode(c,s.camera_tipno.raw/1024);}
            else mode_seen=s.camera_tipno;
        }
        if(s.model_robot_ok&&s.camera_tipno) { s.camrec_regno=s.camera_tipno;if(s.camera_tipno==v(2)){s.hud_speed_visible=true;visible(c,c.ids.p_arac_golge,true);}s.camera_tipno=Var{};mode(c,0); }
        if(!s.model_robot_ok&&s.camrec_regno) {s.camera_tipno=s.camrec_regno;s.camrec_regno=Var{};mode(c,s.camera_tipno.raw/1024,true);}
        if(input.key_c&&!s.model_robot_ok) {if(!c_latch){s.camera_tipno=var_modulo(s.camera_tipno+v(1),v(4));mode(c,s.camera_tipno.raw/1024);c_latch=true;}}else c_latch=false;
        const auto speed=var_min(var_abs(s.arac_ivme),v(350));
        squash=var_clamp(squash+var_sign(speed-old_speed)*v(.5)*var_cos(squash*v(1.2)-v(60))*t*var_abs(var_clamp(speed-old_speed,v(-30),v(30))),v(0),v(100));
        const auto factor=(v(76)-var_min(s.camera_arc,v(76)))/v(16);
        const auto squash_pitch=v(-1.410)+(v(1.051)-v(-1.410))*squash/v(100)*factor+v(1.2)*(var_min(s.camera_arc,v(76))-v(60))/v(16);
        const auto squash_up=v(-.785)*squash/v(100)*factor;
        const auto squash_dist=v(-11.438)+(v(47.586)-v(-11.438))*squash/v(100)*factor;
        old_speed+=v(.2)*(speed-old_speed)*t;if(snap)old_speed=v(350);
        const auto far_step=v(6.25)/v(.5)*t;
        far=s.camera_tipno==v(1)?var_min(far+far_step,v(100)):var_max(far-far_step,v(0));
        turbo=smooth(turbo,s.arac_turbo+s.start_turbo,v(.2),t);
        if(!s.level_restart_no) {
            const auto multiplier=robot_camera?v(2):v(1.5);
            const auto dist_step=v(6.25)/v(.396)*t*multiplier;
            robot_dist=robot_camera?var_min(robot_dist+dist_step,v(220)):var_max(robot_dist-dist_step,v(0));
            const auto old_pitch=pitch,pitch_step=v(6.25)/v(2.292)*t*multiplier;
            pitch=robot_camera?var_min(pitch+pitch_step,v(50.678)):var_max(pitch-pitch_step,v(12.69));target_pitch-=old_pitch-pitch;
            const auto height_step=v(6.25)/v(1.096)*t*multiplier;
            height=robot_camera?var_min(height+height_step,v(123.468)):var_max(height-height_step,v(44.09));
            const auto arc_step=v(6.25)/v(8.706)*t*multiplier;
            robot_arc=robot_camera?var_min(robot_arc+arc_step,v(10)):var_max(robot_arc-arc_step,v(0));
            pan=smooth(pan,target_angles.x-v(180),v(.3),t,true,snap);
            target_pitch=smooth(target_pitch,pitch-target_angles.y,v(.1),t,true,snap);
            const auto effective_pan=pan+s.cam_path_pan+orbit,effective_pitch=target_pitch+s.cam_path_tilt+squash_pitch+v(.542)*far/v(100);
            auto xy=v(232.96)+robot_dist+s.cam_path_dist+squash_dist+v(50)*far/v(100),z=v(232.96)+s.cam_path_dist;
            auto lateral=var_vec_rotate({Var{},s.r_kayma_fark,Var{}},target_angles);
            auto chase=[&](bool clear_roll=true) {
                xy+=turbo*v(8);z+=turbo*v(4);
                const ScriptVec position{
                    target_position.x+var_cos(effective_pan)*xy*var_cos(effective_pitch)-lateral.x,
                    target_position.y+var_sin(effective_pan)*xy*var_cos(effective_pitch)-lateral.y,
                    target_position.z+var_sin(effective_pitch)*z};
                s.camera_position=position;
                camera_direction=var_vec_to_angle(ScriptVec{
                    target_position.x-(lateral.x+s.cam_path_xy*var_cos(target_angles.x+v(90))),
                    target_position.y-(lateral.y+s.cam_path_xy*var_sin(target_angles.x+v(90))),
                    target_position.z+height+s.cam_path_up+squash_up}-position,camera_direction);
                if(clear_roll)camera_direction.z=Var{};
            };
            if(s.camera_tipno<v(2))chase();
            else if(s.camera_tipno==v(2)) {
                s.camera_position={
                    body_position.x+var_cos(body_angles.x)*v(-31.999)*var_cos(body_angles.y-v(49.939)),
                    body_position.y+var_sin(body_angles.x)*v(-31.999)*var_cos(body_angles.y-v(49.939)),
                    body_position.z+var_sin(body_angles.y-v(49.939))*v(-31.999)};
                camera_direction={smooth(camera_direction.x,body_angles.x,v(1.8),t,true,snap),smooth(camera_direction.y,body_angles.y-v(8.959),v(.6),t,true,snap),smooth(camera_direction.z,body_angles.z,v(.6),t,true,snap)};
                auto digits=var_integer(var_abs(s.arac_ivme)*v(.8));
                c.visuals.at(c.ids.p_ic_rakam2).skin=(var_integer(digits/v(100))+v(1)).raw/1024;digits-=var_integer(digits/v(100))*v(100);
                c.visuals.at(c.ids.p_ic_rakam1).skin=(var_integer(digits/v(10))+v(1)).raw/1024;digits-=var_integer(digits/v(10))*v(10);
                c.visuals.at(c.ids.p_ic_rakam0).skin=(digits+v(1)).raw/1024;c.animate(c.ids.p_ic_turbo,"Frame",s.turbo_ekle_miktar/v(3));
            } else { lateral=var_vec_rotate({v(70),Var{},v(22)},body_angles);s.camera_position=body_position+lateral;camera_direction=body_angles; }
            auto angles=camera_direction;
            if(s.arac_ivme>v(77)&&s.camera_tipno<v(2)) {
                shake_index=var_modulo(shake_index+v(1),v(100));
                const auto noise=(v(shake_table[shake_index.raw/1024])-v(50))*(s.arac_ivme-v(77))/v(138240);
                angles.x+=noise;angles.y+=noise;
            }
            if(s.bombacam_salla_s) {
                shake_index=var_modulo(shake_index+v(1),v(100));angles.x+=s.bombacam_salla_m*(v(shake_table[shake_index.raw/1024])-v(50))/v(10);
                shake_index=var_modulo(shake_index+v(1),v(100));angles.y+=s.bombacam_salla_m*(v(shake_table[shake_index.raw/1024])-v(50))/v(10);
            }
            s.camera_angles=angles;
            const auto base_fov=(s.camera_tipno==v(3)?v(70):v(60))+robot_arc+var_max(s.arac_ivme-v(100),v(0))/v(15.625)*(v(.5)+v(.5)*s.cam_path_ivme);
            const auto fov=base_fov+(s.camera_tipno==v(3)?(turbo-v(1))*v(4):Var{});
            s.camera_arc=s.camera_tipno==v(2)?v(60):smooth(s.camera_arc,fov,v(.1),t,false,snap);if(s.camera_tipno==v(2))turbo=s.arac_turbo+s.start_turbo;
            if(robot_seen!=s.camera_tip_no){robot_camera=bool(s.camera_tip_no);robot_seen=s.camera_tip_no;}
            if(s.game_over_ok&&s.camera_tipno>=v(2)) {
                s.camrec_regno=s.camera_tipno;s.camera_tipno=Var{};mode(c,0);visible(c,c.ids.p_arac_tekerler,false);chase(false);
                angles=camera_direction;
                if(s.arac_ivme>v(77)) {
                    shake_index=var_modulo(shake_index+v(1),v(100));
                    const auto noise=(v(shake_table[shake_index.raw/1024])-v(50))*(s.arac_ivme-v(77))/v(138240);
                    angles.x+=noise;angles.y+=noise;
                }
                s.camera_angles=angles;
                s.camera_arc=v(60)+robot_arc+var_max(s.arac_ivme-v(100),v(0))/v(15.625)*(v(.5)+v(.5)*s.cam_path_ivme);
            }
        }
        publish_bloom(s);if(s.game_over_ok){camera_done=true;return;}
        if(input.mouse_right)orbit=var_ang(orbit+v(10)*input.mouse_force_x);
        else {const auto old=orbit;orbit-=orbit*v(.1)*var_sqrt(v(2)*t);if(var_sign(old)!=var_sign(orbit))orbit=Var{};}
    }
    void playback(GameContext& c) {
        auto& s=c.state;visible(c,c.ids.p_ghost_arac,false);visible(c,c.ids.p_ghost_robo,false);
        if(!local||!s.active_ghost)return;
        auto sampled=sample_ghost(*local,s.t_oyun_zaman);if(!sampled)return;
        const auto& r=sampled->record;const EntityId id=r.mode?c.ids.p_ghost_robo:c.ids.p_ghost_arac;
        auto& body=c.physics.body(id);body.position=native(r.position);body.angles=native(r.angles);body.flags.passable=true;
        const auto distance=var_vec_dist(var_vec_scale(r.position,v(.001)),var_vec_scale(body_source(c.physics.body(c.ids.p_camera_target).position),v(.001)))*v(1000);
        auto& visual=c.visuals.at(id);
        if(r.mode&&distance>=v(3000)) {
            // Original far-robot branch writes hidden CAR alpha, retaining robot alpha.
            auto& car=c.visuals.at(c.ids.p_ghost_arac);
            car.alpha=var_min(v(100),v(50)+(distance-v(3000))/v(10));
            if(car.alpha>=v(100))car.transparent=false;
            ++car.revision;
        } else if(distance>=s.min_ghost_dist) {
            visual.alpha=ghost_alpha(distance,s.min_ghost_dist);
            if(distance<v(3000))visual.transparent=true;
            else if(visual.alpha>=v(100))visual.transparent=false;
        }
        visual.invisible=distance<s.min_ghost_dist;
        if(visual.invisible){visual.alpha=v(50);visual.transparent=false;}
        ++visual.revision;
        if(r.mode){c.animate(id,actions.at(r.action),r.frame);if(sampled->action_transition)c.transition(id,actions.at(sampled->next_action),sampled->next_frame,sampled->blend);}
    }
    void begin_record(GameContext& c) {
        auto& s=c.state;
        recording.records.clear();
        recording.records.push_back(record(c,Var{},true));
        record_started=true;
        s.checkpoint_no=physics::Var::literal(0);
        s.checkpoint_oldframe=physics::Var::literal(0);
        s.rakip_ismi=" ";
        s.ghost_rekor=physics::Var{};
        s.ghost_point_time.fill(physics::Var{});
        s.active_ghost=local&&s.selected_ghost==v(1)&&local->record_ticks>v(0);
        if(s.active_ghost) {
            s.rakip_ismi=local->name;
            s.ghost_rekor=local->record_ticks;
            s.ghost_point_time=local->checkpoint_ticks;
            const auto& first=local->records.front();
            auto& car=c.physics.body(c.ids.p_ghost_arac);car.position=native(first.position);car.angles=native(first.angles);
        }
        s.timepanel_time_str="ZAMAN 00:00.000";
        if(s.ghost_rekor.raw>0)source_time_text(s.timepanel_rekor_str,s.ghost_rekor,"REKOR ");
        else s.timepanel_rekor_str="REKOR --:--.---";
        s.timepanel_lap_str="TUR   1/2";
        s.timepanel_check_str=" ";
        s.checkpoint_event_no=0;
        s.checkpoint_delta_ticks=physics::Var{};
        s.hud_visible=true;
        visible(c,c.ids.p_ghost_arac,false);
        visible(c,c.ids.p_ghost_robo,false);
    }
    void race(GameContext& c,const FrameTiming& timing) {
        using physics::Var;
        auto& s=c.state;
        // proc_late captures slot zero before waiting for ghost_yaris_start.
        if(!record_started)begin_record(c);
        if(s.game_over_ok.raw||!s.ghost_yaris_start.raw||s.ghost_reset_ok.raw)return;
        clock=clock+timing.originaltime_step_ticks;
        s.t_oyun_zaman=source_time_roundtrip(clock);
        source_time_text(s.timepanel_time_str,s.t_oyun_zaman,"ZAMAN ");
        const auto gate=observe_race_gate(c,progress.next_gate);
        const auto event=progress.observe(gate.eligible,gate.side_sum,number(s.t_oyun_zaman));
        s.checkpoint_no=Var::literal(progress.next_gate);
        s.checkpoint_oldframe=Var::literal(progress.previous_side);
        if(event.type==RaceEventType::checkpoint) {
            s.t_point_zaman[event.checkpoint-1]=s.t_oyun_zaman;
            if(event.second_lap){s.race_lap=Var::literal(2);s.timepanel_lap_str="TUR   2/2";}
            c.sound("ses_checkpoint");
            s.checkpoint_event_counter++;
            s.checkpoint_event_no=event.checkpoint;
            s.checkpoint_delta_ticks=s.t_oyun_zaman-s.ghost_point_time[event.checkpoint-1];
            if(s.active_ghost) {
                const auto delta=s.checkpoint_delta_ticks;
                const auto magnitude=delta.raw<0?Var{}-delta:delta;
                s.timepanel_check_str=source_str_for_num(v(s.checkpoint_event_no))+". KONTROL NOKTASI: "+(delta.raw<=0?"-":"+");
                source_time_text(s.timepanel_check_str,magnitude,s.timepanel_check_str);
            }
        }
        const bool finish=event.type==RaceEventType::finish;
        if((s.t_oyun_zaman-last_sample).raw>=4*1024||finish) {
            if(recording.records.size()<ghost_capacity)recording.records.push_back(record(c,s.t_oyun_zaman));
            else capacity_exceeded=true;
            last_sample=s.t_oyun_zaman;
        }
        if(finish) {
            c.sound("ses_checkfinish");
            visible(c,c.ids.p_ghost_arac,false);visible(c,c.ids.p_ghost_robo,false);
            s.game_over_ok=Var::literal(1);s.smart_menu_islem=Var::literal(1);
            finish_stage=1;finish_deadline=timing.seconds_deadlines+3;finish_frame=timing.frame_index;
            recording.record_ticks=s.t_oyun_zaman;
            recording.checkpoint_ticks=s.t_point_zaman;
            return;
        }
        playback(c);
    }
    void finish(GameContext& c,const FrameTiming& timing) {
        using physics::Var;
        if(!finish_stage)return;
        auto& s=c.state;
        if(finish_stage==1) {
            if(timing.seconds_deadlines<finish_deadline)return;
            s.gameover_panel_acik=Var::literal(1);finish_stage=2;finish_frame=timing.frame_index;
            return;
        }
        if(finish_stage==2) {
            if(timing.frame_index<finish_frame+2)return;
            if(c.audio.playing(s.turbo_ses_handle2))c.audio.stop(s.turbo_ses_handle2);
            // Source panel-off hides HUD without clearing oyunici_panel_acik.
            // Here black_alpha is GOSP_fon_panel's result-art fade, not a black curtain.
            ++s.hud_off_event_counter;
            s.hud_visible=false;s.hud_speed_visible=false;s.black_visible=true;s.black_alpha=Var{};
            recording.name=s.oyuncu_ismi;
            const bool best=!local||recording.record_ticks<local->record_ticks;
            if(recording.records.size()>ghost_import_capacity||capacity_exceeded||best) {
                try {
                    if(recording.records.size()>ghost_import_capacity||capacity_exceeded) {
                        save_ghost_archive(recording);
                        s.record_error="Record retained in native-recording.rc4; original ghost format allows only 4998 samples";
                    } else {
                        save_local_ghost(recording);local=recording;
                        s.local_ghost_available=true;s.local_ghost_ticks=local->record_ticks;
                        s.local_ghost_name=local->name;s.record_error.clear();
                    }
                } catch(const GhostSaveError& e) {
                    s.record_error=e.what();
                    try {save_ghost_archive(recording);}
                    catch(const GhostSaveError& archive_error){s.record_error+="; native recording archive: ";s.record_error+=archive_error.what();archive_pending=true;}
                }
            }
            finish_stage=3;finish_frame=timing.frame_index;
            return;
        }
        if(finish_stage==3) {
            if(timing.frame_index==finish_frame)return;
            s.black_alpha=finish_fade;
            finish_fade=finish_fade+v(6.25)*timing.originaltime_step_ticks;
            if(finish_fade.raw<=100*1024)return;
            s.black_alpha=Var::literal(100);s.finish_buttons_visible=true;s.mouse_mode=v(2);s.phase=GamePhase::results;finish_stage=0;
        }
    }
};
RaceCamera::RaceCamera():impl_(std::make_unique<Impl>()){}
RaceCamera::~RaceCamera()=default;
void RaceCamera::initialize(GameContext& c) {
    // C4.wdl:59,71,83,95 selects GA_Gorus_Mesafesi; main:254 publishes it before any wait.
    static constexpr std::array<int,4> source_clip_far{55000,112500,280000,280000};
    c.state.camera_clip_far=v(source_clip_far.at(c.world.source_profile.cgm));
    impl_->lifecycle.initialize(c);impl_->recording.records.reserve(ghost_capacity);
    try{impl_->local=load_ghost(local_ghost_path());if(!impl_->local)impl_->local=load_ghost(c.asset_root/"bhlt.rc4");}
    catch(const std::exception& e){c.state.record_error=e.what();impl_->local.reset();}
    c.state.local_ghost_available=impl_->local.has_value();if(impl_->local){c.state.local_ghost_ticks=impl_->local->record_ticks;c.state.local_ghost_name=impl_->local->name;}
}
void RaceCamera::reset(GameContext& c) {
    auto& p=*impl_;
    if(p.archive_pending) {
        try {save_ghost_archive(p.recording);p.archive_pending=false;}
        catch(const GhostSaveError& e){c.state.record_error=e.what();throw;}
    }
    p.clock=p.last_sample=physics::Var{};p.progress.reset();
    p.camera_started=p.camera_done=p.record_started=false;p.capacity_exceeded=false;p.finish_stage=0;
    p.finish_deadline=0;p.finish_fade=physics::Var{};p.recording.records.clear();p.intro_started=false;p.intro_stage=0;
    c.state.t_oyun_zaman=physics::Var{};c.state.race_lap=physics::Var::literal(1);
    c.state.finish_buttons_visible=false;
    if(c.state.phase!=GamePhase::menu&&c.state.phase!=GamePhase::selection) {
        c.state.phase=GamePhase::countdown;p.lifecycle.reset(c,false);
    }
}
void RaceCamera::update_early(GameContext& c,const InputSnapshot& input,const FrameTiming& timing) {
    auto& p=*impl_;auto& s=c.state;if(s.freeze_mode)return;
    if(s.phase==GamePhase::intro) {
        s.loading_visible=p.intro_stage<2;s.loading_stage=2;
        if(!p.intro_started) {
            s.mouse_mode=Var{}; // ana_menu.wdl:3088, before the initial wait and camera intro.
            p.intro_started=true;p.intro_angle=Var{};p.intro_stage=0;
            ++s.history_epoch; // Actual intro VIEW initialization, not the following orbit samples.
            const auto position=body_source(c.physics.body(c.ids.p_aracgovde).position);
            const ScriptVec camera_position{
                position.x+var_cos(p.pan)*v(232.96)*var_cos(p.pitch),
                position.y+var_sin(p.pan)*v(232.96)*var_cos(p.pitch),
                position.z+var_sin(p.pitch)*v(232.96)};
            s.camera_arc=v(60);s.camera_position=camera_position;
            const auto angles=var_vec_to_angle(position+ScriptVec{Var{},Var{},v(40)}-camera_position,s.camera_angles);
            s.camera_angles=angles;p.intro_pan=angles.x;
            s.oyun_oynanabilir=Var{};s.loading_progress=(v(924)+s.oyun_oynanabilir)/v(1024);
            publish_bloom(s);
            return;
        }
        if(p.intro_stage==0) {
            s.oyun_oynanabilir=p.intro_angle/v(8);
            s.camera_angles.x=p.intro_pan+p.intro_angle;
            p.intro_angle+=v(6.25)/v(.25)*timing.originaltime_step_ticks;
            publish_bloom(s);
            if(p.intro_angle<v(800)){s.loading_progress=(v(924)+s.oyun_oynanabilir)/v(1024);return;}
            s.oyun_oynanabilir=v(100);s.loading_progress=v(1);
            // ana_menu stops load_music after camera readiness, before its first wait.
            if(c.audio.playing(s.ses_loadhandle))c.audio.stop(s.ses_loadhandle);
            p.intro_stage=1;p.intro_done_frame=timing.frame_index;
            return;
        }
        if(timing.frame_index<=p.intro_done_frame)return;
        // ana_menu waits one frame before hiding loading UI, then one before HUD launch.
        if(p.intro_stage==1) {
            s.loading_visible=false;s.loading_stage=3;p.intro_stage=2;p.intro_done_frame=timing.frame_index;
            return;
        }
        // bloom_start_code publishes readiness and calls set_gamma_ramp here.
        s.bloom_fx_ok=v(1);s.gamma_corrected_active=true;
        s.loading_visible=false;s.loading_stage=0;s.oyunici_panel_acik=v(1);s.hud_visible=true;
        s.phase=GamePhase::countdown;p.lifecycle.reset(c,true);
    }
    if(s.phase==GamePhase::countdown||s.phase==GamePhase::race)p.lifecycle.update(c,input,timing);
    if(s.bombacam_basla_t==v(1)) {
        if(s.bombacam_salla_m<s.bombacam_basla_m) {
            p.shake_amount=s.bombacam_basla_m;p.shake_duration=s.bombacam_basla_z?s.bombacam_basla_z:v(.5);
            p.shake_phase=Var{};s.bombacam_salla_id=var_integer(c.random(v(1000))+c.random(v(985)));
        }
        s.bombacam_basla_t=Var{};
    }
    if(p.shake_amount) {
        s.bombacam_salla_m=p.shake_amount*(v(100)-p.shake_phase)/v(100);s.bombacam_salla_s=v(1);
        p.shake_phase+=v(6.25)/p.shake_duration*timing.originaltime_step_ticks;
        if(p.shake_phase>=v(100)||s.ghost_reset_ok){p.shake_amount=Var{};s.bombacam_salla_s=s.bombacam_salla_m=Var{};}
    }
    p.finish(c,timing);
}
void RaceCamera::update_late(GameContext& c,const InputSnapshot& input,const FrameTiming& timing) {
    if(c.state.freeze_mode||c.state.ghost_reset_ok||c.state.phase==GamePhase::intro||c.state.phase==GamePhase::menu||c.state.phase==GamePhase::selection)return;
    impl_->race(c,timing);impl_->camera(c,input,timing);
}
std::string RaceCamera::diagnostics() const {std::ostringstream out;out<<"race_records="<<impl_->recording.records.size()<<" gate="<<impl_->progress.next_gate<<" clock_ticks="<<number(impl_->clock)<<" camera_mode="<<number(impl_->mode_seen)<<" ghost="<<bool(impl_->local)<<" numeric/scheduler/RNG parity unproven";return out.str();}
} // namespace c4::game
