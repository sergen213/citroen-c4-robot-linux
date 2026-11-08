#include "RobotController.hpp"
#include "RecoveredLogic.hpp"
#include "physics/NativeNumeric.hpp"
#include <array>

namespace c4::game {
namespace {
using V = physics::Var;
using physics::v;
using physics::body_source;
using physics::property;
using physics::number;
using physics::native;
using physics::var_abs;
using physics::var_min;
using physics::var_max;
using physics::var_clamp;
V sin_d(V a) { return physics::var_sin(a); }
V cos_d(V a) { return physics::var_cos(a); }
V angle(V a) { return physics::var_ang(a); }
ScriptVec bearing(ScriptVec d,ScriptVec previous) { return physics::var_vec_to_angle(d,previous); }
ScriptVec angles_between(ScriptVec from, ScriptVec to, V numerator, V denominator) {
    return {angle(to.x-from.x)*numerator/denominator+from.x,
            angle(to.y-from.y)*numerator/denominator+from.y,
            angle(to.z-from.z)*numerator/denominator+from.z};
}
std::array<EntityId,6> robot_parts(const ComponentIds& id) { return {id.p_robot_teker,id.p_robot_mekanik,id.p_robot_farcam,id.p_robot_fargovde,id.p_robot_camlar,id.p_robot_kaporta}; }
std::array<EntityId,9> car_parts(const ComponentIds& id) { return {id.p_arac_cam,id.p_far_govde,id.p_far_cam,id.p_arac_ontakim,id.p_arac_koltuk,id.p_arac_tekerler,id.p_arac_direx,id.p_arac_kaporta,id.p_arac_ayna}; }
const std::array<V,24> lengths{v(0),v(514.259),v(374.389),v(1416.102),v(856.401),v(687.854),v(662.203),v(802.566),v(788.879),v(335.520),v(270.139),v(262.459),v(111.684),v(375.226),v(312.627),v(1406.502),v(1800.806),v(237.400),v(234.815),v(215.343),v(1523.602),v(1512.968),v(344.161),v(529.398)};
bool hanging(int p) { return p==2||p==17||p==18||p==19; }
V min_y(int p) { return v(p==2?20735:p==17?5714:p==18?-3375:p==19?-12658:0); }
constexpr physics::Mode movement=physics::Mode::ignore_passable|physics::Mode::ignore_content|physics::Mode::ignore_push|physics::Mode::ignore_sprites|physics::Mode::glide;
constexpr physics::Mode ground=physics::Mode::ignore_me|physics::Mode::ignore_passable|physics::Mode::ignore_content|physics::Mode::ignore_push|physics::Mode::ignore_sprites|physics::Mode::ignore_flag2|physics::Mode::use_box;
}
void RobotController::pose(GameContext& c,std::string_view clip,V percent,bool cycle,bool target) {
    if(!target)c.copy_pose(c.ids.p_camera_target,c.ids.p_aracgovde);
    for(auto id:robot_parts(c.ids)) { c.animate(id,clip,percent,cycle); c.copy_pose(id,target?c.ids.p_camera_target:c.ids.p_aracgovde); }
}
void RobotController::blend(GameContext& c,std::string_view clip,V percent,V blend_percent) {
    for(auto id:robot_parts(c.ids))c.transition(id,clip,percent,blend_percent);
}
void RobotController::move(GameContext& c,V t,V yaw,Vec3 relative,bool slip,bool ground_done) {
    if(!ground_done)contact_.robot_ground_angles(c,t,yaw);
    const V collision=contact_.collision(c,t,true);
    const auto a=body_source(c.physics.body(c.ids.p_aracgovde).angles);
    const V pan=slip?c.state.arac_gidis_pan:a.x;
    c.state.arac_cekimkuvveti={v(.689)*c.state.arac_ivme*t*cos_d(pan)*cos_d(a.y),v(.689)*c.state.arac_ivme*t*sin_d(pan)*cos_d(a.y),v(.689)*c.state.arac_ivme*t*sin_d(a.y)};
    c.state.body_skill2=property(c.physics.move(c.ids.p_aracgovde,relative,native(c.state.arac_cekimkuvveti),movement).distance);
    contact_.gravity(c,t,collision);
    if(collision==v(1)&&phase_>=Phase::running&&phase_<=Phase::brake)sparks(c,-1);
}
void RobotController::ghost_run(GameContext& c,V t,bool advance) {
    c.state.robot_action_no=v(1); c.state.robot_frame_no=c.state.temp_ghostframe;
    if(advance)c.state.temp_ghostframe=physics::var_modulo(c.state.temp_ghostframe+v(6.25)/(v(1.4)-(v(.5)*(var_min(c.state.arac_ivme,v(280))/v(280))))*t,v(100));
}
void RobotController::stop_skid(GameContext& c) { if(c.audio.playing(skid_))c.audio.stop(skid_); }
void RobotController::skid(GameContext& c,bool rail) {
    if(!c.audio.playing(skid_))skid_=c.sound(rail?"robot_kay_demir_wav":"robot_kay_asfalt_wav",v(10),true);
    const V speed=var_min(c.state.arac_ivme,v(280))/v(280);
    const V volume=(rail?v(30):v(80))*speed+(rail?v(70):v(20));
    const V frequency=v(100)/(v(1.4)-(rail?v(.4):v(.7))*speed);
    c.audio.tune(skid_,volume.to_float(),frequency.to_float(),0);
}
void RobotController::footsteps(GameContext& c,V percent) {
    constexpr std::array<std::string_view,4> sounds{"robot_adim_1_wav","robot_adim_2_wav","robot_adim_3_wav","robot_adim_4_wav"};
    const V duration=v(1.4)-(v(.5)*(var_min(c.state.arac_ivme,v(280))/v(280)));
    if(percent>=v(0)&&step_==0){const auto h=c.sound(sounds[physics::var_integer(c.random(v(400))/v(100)).raw/1024],v(100));c.audio.tune(h,0,(v(140)/duration).to_float(),0);step_=1;}
    if(percent>=v(47.368)&&step_==1){const auto h=c.sound(sounds[physics::var_integer(c.random(v(400))/v(100)).raw/1024],v(100));c.audio.tune(h,0,(v(150)/duration).to_float(),0);step_=2;}
}
void RobotController::dust(GameContext& c,int foot) {
    const auto id=c.ids.p_robot_mekanik;const auto sample=c.visuals[id].sample;
    const auto a=body_source(c.physics.vec_for_vertex(id,foot==0?2563:2065,sample));
    const auto b=body_source(c.physics.vec_for_vertex(id,foot==0?2565:2067,sample));
    c.effect("robotkum_toz_cik",native(physics::var_vec_scale({a.x+b.x,a.y+b.y,a.z+b.z},v(.5))),{},id);
}
void RobotController::sparks(GameContext& c,int foot) {
    if(foot<0){c.effect("kivilci_cik_fx",native(c.state.crash_target),{},c.ids.p_aracgovde);return;}
    const auto id=c.ids.p_robot_mekanik;const auto sample=c.visuals[id].sample;
    const auto a=body_source(c.physics.vec_for_vertex(id,foot==0?2563:2065,sample));
    const auto b=body_source(c.physics.vec_for_vertex(id,foot==0?2565:2067,sample));
    c.effect("kivilci_kay_fx",native(physics::var_vec_scale({a.x+b.x,a.y+b.y,a.z+b.z},v(.5))),{},id);
}
void RobotController::begin_shadow(GameContext& c,bool robot) {
    shadow_percent_=v(0);shadow_active_=true;shadow_to_robot_=robot;
    auto& car=c.visuals[c.ids.p_arac_golge];auto& robo=c.visuals[c.ids.p_robot_golge];
    car.transparent=true;robo.transparent=true;car.invisible=false;robo.invisible=false;
    car.alpha=robot?v(100):v(0);robo.alpha=robot?v(0):v(100);
    shadow_percent_+=v(6.25)/v(.7)*current_tick_;
}
void RobotController::initialize(GameContext& c,FrameTiming timing) {
    current_tick_=timing.originaltime_step_ticks;
    phase_=Phase::conversion; i_=v(0); contact_.reset();
    c.state.temp_ghostframe=v(0);c.state.arac_vites_ivme=v(0);c.state.arac_vites_no=v(0);c.state.motor_ses_ivme=v(0);
    c.sound("robot_donus_wav");
    begin_shadow(c,true);
}
void RobotController::reset(GameContext& c) {
    stop_skid(c);phase_=Phase::inactive;contact_.reset();
}
void RobotController::clear_camera(GameContext& c) {
    c.state.cam_path_pan=v(0);c.state.cam_path_tilt=v(0);c.state.cam_path_up=v(0);c.state.cam_path_xy=v(0);c.state.cam_path_dist=v(0);c.state.cam_path_ivme=v(1);
}
bool RobotController::hanging_dismount_allowed(V y) noexcept {
    return y>v(20880)||(y<v(19780)&&y>v(5830))||(y<v(4715)&&y>v(-3190))||(y<v(-4370)&&y>v(-12435))||y<v(-13900);
}
void RobotController::update_auxiliary(GameContext& c,FrameTiming timing) {
    if(c.state.freeze_mode!=v(0))return;
    const V t=timing.originaltime_step_ticks;
    if(shadow_active_) {
        auto& car=c.visuals[c.ids.p_arac_golge];auto& robo=c.visuals[c.ids.p_robot_golge];
        if(shadow_percent_<=v(100)){car.alpha=shadow_to_robot_?v(100)-shadow_percent_:shadow_percent_;robo.alpha=v(100)-car.alpha;shadow_percent_+=v(6.25)/v(.7)*t;}
        else {
            shadow_active_=false;car.transparent=false;
            if(shadow_to_robot_){car.invisible=true;car.alpha=v(100);} // Source writes alpha=0, then overwrites it with 100.
            else {robo.invisible=true;robo.alpha=v(0);robo.transparent=false;car.alpha=v(100);}
        }
    }
}
void RobotController::update(GameContext& c,const InputSnapshot& input,FrameTiming timing) {
    if(c.state.ghost_reset_ok!=v(0)||c.state.freeze_mode!=v(0)||phase_==Phase::inactive)return;
    const V t=timing.originaltime_step_ticks;
    current_tick_=t;
    if(phase_==Phase::conversion) {
        if(i_>v(100)){c.sound("robot_zipla_dus_wav");c.state.jump_delay_time=v(0);c.state.arac_ivme=var_max(v(0),c.state.arac_ivme);enter(c,Phase::running);locomotion(c,input,t);return;}
        move(c,t,v(0),{},true);c.state.robot_action_no=v(0);c.state.robot_frame_no=i_;pose(c,"donus",i_);
        i_+=v(6.25)/(i_<v(86.666)?v(.7):v(.925))*t;contact_.shadows(c);return;
    }
    if(phase_>=Phase::reverse_blend&&phase_<=Phase::reverse_inis){reverse(c,t);return;}
    if(phase_>=Phase::free_jump){jump(c,input,t);return;}
    locomotion(c,input,t);
}
void RobotController::reverse(GameContext& c,V t) {
    auto& s=c.state;
    if(phase_==Phase::reverse_blend) {
        if(i_>v(100)){pose(c,"arac_ol",v(16.7));i_=v(16.7);phase_=Phase::reverse_fold;reverse_start_=i_;lateral_start_=s.r_kayma_fark;reverse_speed_=v(1)+(v(.1)*(var_min(s.arac_ivme,v(220))/v(220)));begin_shadow(c,false);}
        else {
            contact_.robot_ground_angles(c,t,v(0));
            s.arac_max_hiz=var_max(v(280),s.arac_max_hiz-v(6.25)/v(.5)*t);s.arac_ivme=var_min(s.arac_ivme,s.arac_max_hiz);
            move(c,t,v(0),{},false,true);pose(c,entry_==1?"bekle_loop":entry_==2?"fren_loop":entry_turn_>v(0)?"yavas_sol":"yavas_sag",entry_==3?var_abs(entry_turn_):v(0));
            blend(c,"arac_ol",v(16.7),i_);i_+=v(6.25)/v(.1)*t;contact_.shadows(c);ghost_run(c,t);return;
        }
    }
    if(phase_==Phase::reverse_fold) {
        if(i_>=v(100)){s.arac_durum_tip=v(0);s.r_kayma_fark=v(0);s.camera_tip_no=v(0);i_=v(60)-(i_-v(100))/v(2.5);phase_=Phase::reverse_donus;}
        else {
            move(c,t);s.r_kayma_fark=lateral_start_*(v(100)-i_)/(v(100)-reverse_start_);pose(c,"arac_ol",i_);s.robot_action_no=v(0);s.robot_frame_no=v(0);
            if(i_<v(16.666))i_+=v(6.25)/v(1)*t*v(2.7)*(v(1)+(v(.2)*(var_min(s.arac_ivme,v(220))/v(220))));
            else i_+=v(6.25)/v(1)*t*v(3.5)*reverse_speed_;
            contact_.shadows(c);return;
        }
    }
    if(phase_==Phase::reverse_donus) {
        if(i_<=v(6.666)){for(auto id:car_parts(c.ids))c.visuals[id].invisible=false;for(auto id:robot_parts(c.ids))c.visuals[id].invisible=true;i_=(v(6.666)-i_)*v(2.5)/v(1.833);phase_=Phase::reverse_inis;}
        else {move(c,t);pose(c,"donus",i_);i_-=v(6.25)/v(2.5)*t*v(3.5)*reverse_speed_;contact_.shadows(c);return;}
    }
    if(phase_==Phase::reverse_inis) {
        if(i_>=v(27.273)){
            s.arac_gidis_pan=body_source(c.physics.body(c.ids.p_aracgovde).angles).x;s.arac_max_hiz=v(280);
            for(auto id:{c.ids.p_fsolteker,c.ids.p_fsagteker,c.ids.p_bsolteker,c.ids.p_bsagteker})c.visuals[id].invisible=false;
            c.visuals[c.ids.p_arac_tekerler].invisible=true;s.model_robot_ok=v(2);
            s.car_settling_frame=i_;s.car_settling_speed=reverse_speed_;s.phase_request=PhaseRequest::car;
            phase_=Phase::inactive;return;
        }
        move(c,t);for(auto id:car_parts(c.ids)){c.animate(id,"inis",i_);c.copy_pose(id,c.ids.p_aracgovde);}c.copy_pose(c.ids.p_camera_target,c.ids.p_aracgovde);
        i_+=v(6.25)/v(1.833)*t*(i_<v(9.09)?v(3.5):i_<v(18.181)?v(5.5):v(7))*reverse_speed_;contact_.shadows(c);
    }
}
void RobotController::acquire_path(GameContext& c) {
    stop_skid(c);auto& s=c.state;const auto& b=c.physics.body(c.ids.p_aracgovde);
    const auto position=body_source(b.position),body_angles=body_source(b.angles);
    auto vertex=[&](int n,V p,unsigned index) { auto id=c.ids.path_ent_dizi[n];c.animate(id,"Frame",p);return body_source(c.physics.vec_for_vertex(id,index,c.visuals[id].sample)); };
    s.path_dist_diz.fill(v(10000));
    for(V p=v(0);p<=v(100);p+=v(10))for(int n=1;n<24;++n){
        const auto anchor=vertex(n,p,4);
        const ScriptVec d{(position.x-anchor.x)/v(1000),(position.y-anchor.y)/v(1000),(position.z-anchor.z)/v(1000)};
        const V distance=physics::var_vec_length(d)*v(1000);
        if(distance<s.path_dist_diz[n])s.path_dist_diz[n]=distance;
    }
    std::array<int,4> top{};
    for(int rank=0;rank<4;++rank){V best=v(10000);for(int n=1;n<24;++n){bool used=false;for(int j=0;j<rank;++j)used|=top[j]==n;if(!used&&s.path_dist_diz[n]<=best){top[rank]=n;best=s.path_dist_diz[n];}}}
    for(int rank=0;rank<4;++rank)s.path_top_no[rank]=v(top[rank]);
    V best=v(10000);selected_no_=0;
    ScriptVec facing{};
    for(V p=v(0);p<=v(100);p+=v(.2))for(int n:top){
        const auto anchor=vertex(n,p,4);
        facing=bearing({anchor.x-position.x,anchor.y-position.y,anchor.z-position.z},facing);
        if(var_abs(angle(facing.x-body_angles.x))>(hanging(n)?v(30):v(40)))continue;
        const ScriptVec d{(position.x-anchor.x)/v(1000),(position.y-anchor.y)/v(1000),(position.z-anchor.z)/v(1000)};
        const V distance=physics::var_vec_length(d)*v(1000);if(distance>=best)continue;
        const auto tip=vertex(n,p,1);const auto tangent=bearing({tip.x-anchor.x,tip.y-anchor.y,tip.z-anchor.z},tip);const int direction=var_abs(angle(body_angles.x-tangent.x))<=v(90)?1:-1;
        const V travel=lengths[n]/(var_max(s.arac_ivme,v(40))*v(.278));
        if((direction==1?travel*(v(100)-p)/v(100):travel*p/v(100))>v(1.3)||travel==v(0)){best=distance;selected_no_=n;path_frame_=p;s.selected_path=c.ids.path_ent_dizi[n];}
    }
    lateral_start_=s.r_kayma_fark;i_=v(0);
    if(best>=v(500)||!selected_no_||(hanging(selected_no_)&&position.y<=min_y(selected_no_))){phase_=Phase::free_jump;c.sound("robot_zipla_wav");return;}
    path_length_=lengths[selected_no_];travel_time_=path_length_/(var_max(s.arac_ivme,v(40))*v(.278));
    auto anchor=vertex(selected_no_,path_frame_,4);const auto tip=vertex(selected_no_,path_frame_,1);const auto a=bearing({tip.x-anchor.x,tip.y-anchor.y,tip.z-anchor.z},tip);direction_=var_abs(angle(body_angles.x-a.x))<=v(90)?1:-1;
    if(travel_time_!=v(0)&&(direction_==1?travel_time_*(v(100)-path_frame_)/v(100):travel_time_*path_frame_/v(100))<=v(1.3)){phase_=Phase::free_jump;c.sound("robot_zipla_wav");return;}
    s.collision_ok=v(0);s.crash_hedef=v(-666);s.crash_totaltime=v(100);
    if(c.audio.playing(s.carp_loop_handle))c.audio.stop(s.carp_loop_handle);
    if(hanging(selected_no_)){c.physics.body(s.selected_path).position.x=number(direction_==1?v(-4143):v(-4119));anchor=vertex(selected_no_,path_frame_,4);phase_=Phase::hang_entry;c.sound("robot_zipla_wav");}
    else{phase_=Phase::rail_entry;c.sound("robot_raya_cik_wav");}
    alignment_position_={anchor.x-position.x,anchor.y-position.y,anchor.z-position.z};alignment_angles_=body_angles;stop_time_=v(0);
    ground_probe_angles_={};
}
void RobotController::advance_path(V t) { if(travel_time_>v(0))path_frame_+=v(6.25)/travel_time_*t*v(direction_);path_frame_=var_clamp(path_frame_,v(0),v(100)); }
void RobotController::path_pose(GameContext& c,PathAngleDestination destination) {
    const auto id=c.state.selected_path;c.animate(id,"Frame",path_frame_);
    auto& target=c.physics.body(c.ids.p_camera_target);const auto p=body_source(c.physics.vec_for_vertex(id,4,c.visuals[id].sample));
    const auto tip=body_source(c.physics.vec_for_vertex(id,1,c.visuals[id].sample));
    const ScriptVec previous=destination==PathAngleDestination::tip?tip:destination==PathAngleDestination::anchor?p:body_source(target.angles);
    auto a=bearing({tip.x-p.x,tip.y-p.y,tip.z-p.z},previous);a.x+=v(90)*(v(direction_)-v(1));a.y*=v(direction_);a.z=v(0);
    target.position=native(p);target.angles=native(a);
}
void RobotController::camera_offsets(GameContext& c,V numerator,V denominator,bool hang) {
    auto& s=c.state;
    s.cam_path_pan=(hang?v(-13.456):v(-17.685))*numerator/denominator;
    s.cam_path_tilt=(hang?v(-7.457):v(.615))*numerator/denominator;
    s.cam_path_up=(hang?v(71.499):v(3.953))*numerator/denominator;
    s.cam_path_xy=(hang?v(-76.844):v(-74.280))*numerator/denominator;
    s.cam_path_dist=(hang?v(67.88):v(0))*numerator/denominator;
    if(!hang){s.cam_path_pan*=v(direction_);s.cam_path_xy*=v(direction_);}
    s.cam_path_ivme=(denominator-numerator)/denominator;
}
void RobotController::jump(GameContext& c,const InputSnapshot& input,V t) {
    auto& s=c.state;auto& b=c.physics.body(c.ids.p_aracgovde);auto& target=c.physics.body(c.ids.p_camera_target);
    if(phase_==Phase::free_jump) {
        if(i_>v(100)){c.sound("robot_zipla_dus_wav");clear_camera(c);s.r_kayma_fark=v(0);enter(c,Phase::running,6);locomotion(c,input,t);s.temp_ghostframe=v(0);return;}
        const V steering=v(int(input.key_cul))-v(int(input.key_cur));
        const V yaw=s.game_over_ok!=v(0)||steering==v(0)?v(0):(v(10)-(v(4)*(var_min(s.arac_ivme,v(220))/v(220))))*steering*t*var_min(s.arac_ivme,v(80))/v(80);
        contact_.robot_ground_angles(c,t,yaw);s.arac_ivme=var_max(var_min(s.arac_ivme-v(.576)*t,s.arac_max_hiz),v(0));move(c,t,v(0),{},false,true);
        s.cam_path_pan=v(0);s.cam_path_tilt=v(0);s.cam_path_xy=v(0);s.cam_path_dist=v(0);
        s.cam_path_up=i_<v(55)?v(85.609)*i_/v(55):v(85.609)*(v(100)-i_)/v(45);s.cam_path_ivme=i_<v(55)?(v(55)-i_)/v(55):(i_-v(55))/v(45);
        s.r_kayma_fark=lateral_start_*(v(55)-var_min(i_,v(55)))/v(55);pose(c,"bosa_zipla",i_);s.robot_action_no=i_<v(50)?v(6):v(8);s.robot_frame_no=i_<v(50)?i_*v(2):(i_-v(50))*v(2);i_+=v(6.25)/v(1)*t;contact_.shadows(c);
        if(s.toprak_yol_ok==v(1)&&s.c4_eski_havazaman<v(1)&&i_<v(12)){dust(c,0);dust(c,1);}return;
    }
    const bool hang=phase_==Phase::hang_entry||phase_==Phase::hang_loop||phase_==Phase::hang_exit;
    const auto speed_from_path=[&]{s.arac_ivme=travel_time_>v(0)?path_length_/(travel_time_*v(.278)):v(0);};
    const auto try_ground=[&](bool side){
        const auto id=s.selected_path;c.animate(id,"Frame",path_frame_);
        const auto p=body_source(c.physics.vec_for_vertex(id,4,c.visuals[id].sample));
        const auto tip=body_source(c.physics.vec_for_vertex(id,1,c.visuals[id].sample));auto a=bearing({tip.x-p.x,tip.y-p.y,tip.z-p.z},ground_probe_angles_);a.x+=v(90)*(v(direction_)-v(1));a.y*=v(direction_);a.z=v(0);ground_probe_angles_=a;
        const ScriptVec position=side?ScriptVec{p.x-v(260)*cos_d(a.x-v(90))*v(direction_),p.y-v(260)*sin_d(a.x-v(90))*v(direction_),p.z+v(220)}:ScriptVec{p.x,p.y,p.z+v(100)};
        b.position=native(position);b.angles=native(a);
        const auto hit=c.physics.trace(b.position,native({position.x,position.y,position.z-v(1000)}),ground,c.ids.p_aracgovde);
        const V distance=property(hit.distance);
        if(!hit.trace_hit||distance<=v(0))return false;
        b.position.z=number(body_source(b.position).z-(distance-v(1)));contact_.robot_ground_angles(c,t,v(0),true);return true;
    };
    const auto begin_exit=[&]{const auto p=body_source(b.position),target_p=body_source(target.position);alignment_position_={p.x-target_p.x,p.y-target_p.y,p.z-target_p.z};alignment_angles_=body_source(target.angles);speed_from_path();in_pan_=body_source(b.angles).x;i_=hang?v(0):v(33.5);phase_=hang?Phase::hang_exit:Phase::rail_exit;};
    if(phase_==Phase::rail_entry||phase_==Phase::hang_entry) {
        if(i_<=v(100)) {
            advance_path(t);path_pose(c,PathAngleDestination::tip);const V duration=hang?v(28.6):v(50),percent=var_min(i_,duration);const auto p=body_source(target.position);
            target.position=native({p.x-alignment_position_.x*(duration-percent)/duration,p.y-alignment_position_.y*(duration-percent)/duration,p.z-alignment_position_.z*(duration-percent)/duration});
            target.angles=native(angles_between(alignment_angles_,body_source(target.angles),percent,duration));camera_offsets(c,percent,duration,hang);s.r_kayma_fark=lateral_start_*(duration-percent)/duration;
            pose(c,hang?"tutma_up":direction_==1?"hopsag_up":"hopsol_up",i_,false,true);s.robot_action_no=hang?v(6):direction_==1?v(3):v(9);s.robot_frame_no=var_min(i_*(hang?v(3.45):v(2)),v(100));
            i_+=v(6.25)/(hang?v(1.3):v(1))*t;if(!hang){speed_from_path();if(i_>=v(60)){skid(c,true);sparks(c,0);sparks(c,1);}if(s.toprak_yol_ok==v(1)&&s.c4_eski_havazaman<v(1)&&i_<v(16)){dust(c,0);dust(c,1);}return;}
            if(!(input.key_s&&i_>v(50)&&hanging_dismount_allowed(body_source(target.position).y)))return;
        }
        camera_offsets(c,v(1),v(1),hang);s.r_kayma_fark=v(0);path_pose(c);pose(c,hang?"tutma_up":direction_==1?"hopsag_up":"hopsol_up",v(100),false,true);stop_time_=v(0);i_=v(0);phase_=hang?Phase::hang_loop:Phase::rail_slide;
    }
    if(phase_==Phase::hang_loop) {
        path_pose(c);pose(c,"tutma_loop",i_,true,true);s.robot_action_no=v(7);s.robot_frame_no=i_;i_=physics::var_modulo(i_+v(6.25)/v(.1)*t,v(100));advance_path(t);
        travel_time_=var_min(travel_time_+v(.5)*t*stop_time_/v(100),path_length_/v(11.12));stop_time_+=v(6.25)/v(6)*t;
        const V remaining=travel_time_*(direction_==1?v(100)-path_frame_:path_frame_)/v(100);
        if((remaining<=v(.1)||input.key_s||travel_time_>=path_length_/v(11.12))&&hanging_dismount_allowed(body_source(target.position).y)&&try_ground(false))begin_exit();else return;
    }
    if(phase_==Phase::rail_slide) {
        if(!input.key_s&&travel_time_<path_length_/v(11.12)&&path_frame_<=v(100)&&path_frame_>=v(0)) {
            sparks(c,0);sparks(c,1);
            path_pose(c);pose(c,direction_==1?"hopsag_kay":"hopsol_kay",i_,true,true);s.robot_action_no=direction_==1?v(4):v(10);s.robot_frame_no=v(0);i_=physics::var_modulo(i_+v(25),v(100));advance_path(t);
            if(travel_time_*(direction_==1?v(100)-path_frame_:path_frame_)/v(100)>v(.3)){travel_time_=var_min(travel_time_+v(.5)*t*stop_time_/v(100),path_length_/v(11.12));stop_time_+=v(6.25)/v(4)*t;speed_from_path();skid(c,true);return;}
        }
        i_=v(0);phase_=Phase::rail_ground;
    }
    if(phase_==Phase::rail_ground) {
        path_pose(c);advance_path(t);pose(c,direction_==1?"hopsag_in":"hopsol_in",i_,false,true);s.robot_action_no=direction_==1?v(5):v(11);s.robot_frame_no=v(0);i_+=v(6.25)/v(.8)*t;
        if(i_>=v(33.5)){if(try_ground(true)){stop_skid(c);c.sound("robot_raydan_in_wav");begin_exit();}else i_=v(33.5);}
        if(phase_==Phase::rail_ground){speed_from_path();skid(c,true);return;}
    }
    if(phase_==Phase::rail_exit||phase_==Phase::hang_exit) {
        if(i_>v(100)){clear_camera(c);c.sound("robot_zipla_dus_wav");if(!hang)s.temp_ghostframe=direction_==-1?v(47.368):v(0);enter(c,Phase::running,!hang&&direction_==-1?5:6);locomotion(c,input,t);return;}
        advance_path(t);path_pose(c,PathAngleDestination::anchor);const auto path_angle=body_source(target.angles);
        advance_path(t); // The source intentionally advances twice in both exit loops.
        b.angles.x=number(in_pan_);move(c,t,angle(path_angle.x-body_source(b.angles).x)/v(2));
        const V numerator=hang?i_:i_-v(33.5),denominator=hang?v(100):v(66.5);const auto p=body_source(b.position);
        target.position=native({p.x-alignment_position_.x*(v(100)-i_)/denominator,p.y-alignment_position_.y*(v(100)-i_)/denominator,p.z-alignment_position_.z*(v(100)-i_)/denominator});
        target.angles=native(angles_between(alignment_angles_,body_source(b.angles),numerator,denominator));camera_offsets(c,v(100)-i_,denominator,hang);
        pose(c,hang?"tutma_in":direction_==1?"hopsag_in":"hopsol_in",i_,false,true);s.robot_action_no=hang?v(8):direction_==1?v(5):v(11);s.robot_frame_no=hang?i_:v(100)*(i_-v(33.5))/v(66.5);i_+=v(6.25)/(hang?v(.4):v(.8))*t;if(!hang)speed_from_path();
    }
}
} // namespace c4::game

namespace c4::game {
void RobotController::enter(GameContext& ctx, Phase phase, int entry, physics::Var turn)
{
    if (phase == Phase::reverse_blend || phase == Phase::reverse_fold) {
        phase_ = entry >= 1 && entry <= 3 ? Phase::reverse_blend : Phase::reverse_fold;
        entry_ = entry;
        entry_turn_ = turn;
        i_=v(0);stop_skid(ctx);ctx.sound("robot_donus_wav");
        lateral_start_=ctx.state.r_kayma_fark;reverse_start_=v(0);
        reverse_speed_=v(1)+(v(.1)*(physics::var_min(ctx.state.arac_ivme,v(220))/v(220)));
        if(phase_==Phase::reverse_fold)begin_shadow(ctx,false);
        return;
    }
    phase_ = phase;
    entry_ = entry;
    entry_turn_ = turn;
    if (phase != Phase::running && phase != Phase::slide && phase != Phase::coast &&
        phase != Phase::brake && phase != Phase::idle)
        return;
    i_ = phase == Phase::running && entry == 1 ? v(100) : v(0);
    turn_ = v(0);
    turn_time_ = turn_release_ = titre_ = coast_wait_ = v(0);
    titre_direction_ = 1;
    recenter_ = false;
    entry_d_latched_ = false;
    locomotion_ready_ = false;
    entry_active_ = phase != Phase::running || (entry >= 1 && entry <= 4);
    if (phase == Phase::running) {
        stop_skid(ctx);
        if (entry == 5 || entry == 6) ctx.state.jump_delay_time = v(100);
    } else if (phase == Phase::idle) {
        stop_skid(ctx);
        ctx.state.arac_ivme = v(0);
    }
}

void RobotController::locomotion(GameContext& ctx, const InputSnapshot& input, physics::Var time)
{
    using V = physics::Var;
    auto& s = ctx.state;
    const int steering = int(input.key_cul) - int(input.key_cur);
    const bool active = s.game_over_ok == v(0);
    const bool forward = input.key_cuu && !input.key_cud;
    const bool backward = !input.key_cuu && input.key_cud;
    const bool released = !input.key_cuu && !input.key_cud;
    const auto limit_speed = [&](V rate) {
        s.arac_max_hiz = physics::var_max(v(280), s.arac_max_hiz - v(6.25) / rate * time);
    };
    const auto decelerate = [&](V amount) {
        s.arac_ivme = physics::var_max(v(0), physics::var_min(s.arac_ivme - amount * time, s.arac_max_hiz));
    };
    const auto yaw_for = [&](V base, V reduction, bool low_speed) {
        if (!active || steering == 0) return v(0);
        V yaw = (base - reduction * (physics::var_min(s.arac_ivme, v(220)) / v(220))) * v(steering) * time;
        if (low_speed) yaw = yaw * physics::var_min(s.arac_ivme, v(80)) / v(80);
        return yaw;
    };
    const auto start_reverse = [&](int entry, V turn) {
        enter(ctx, entry >= 1 && entry <= 3 ? Phase::reverse_blend : Phase::reverse_fold, entry, turn);
        reverse(ctx, time);
    };
    const auto start_jump = [&](int /*entry*/, V /*percent*/) {
        // robot_zipla_fx accepts these two values but never reads them.
        acquire_path(ctx);
        jump(ctx, input, time);
    };
    const auto jump_held = [&] { return active && input.key_s && s.jump_delay_time <= v(0); };
    const auto cooldown = [&] { s.jump_delay_time = physics::var_max(v(0), s.jump_delay_time - v(6.25) / v(0.4) * time); };
    const auto shadows_ghost = [&](bool advance = true) {
        contact_.shadows(ctx);
        ghost_run(ctx, time, advance);
    };
    const auto steering_pose = [&](V yaw, V release_rate) {
        if (yaw != v(0) && !recenter_) {
            const V amount = v(6.25) / v(0.4) * time * turn_time_ / v(100);
            turn_ = physics::var_clamp(input.key_cul ? turn_ + amount : turn_ - amount, v(-100), v(100));
            turn_time_ = physics::var_min(v(100), turn_time_ + v(6.25) / v(0.1) * time);
            turn_release_ = v(0);
        } else {
            if (recenter_) turn_release_ = v(100);
            const V amount = v(6.25) / (v(0.6) - v(recenter_) * v(0.3)) * time * turn_release_ / v(100);
            turn_ = turn_ >= v(0) ? physics::var_max(v(0), turn_ - amount) : physics::var_min(v(0), turn_ + amount);
            turn_release_ = physics::var_min(v(100), turn_release_ + v(6.25) / release_rate * time);
            turn_time_ = v(0);
        }
    };
    const auto turned_animation = [&](bool fast) {
        titre_ += v(6.25) / v(0.05) * time * v(titre_direction_);
        if ((titre_direction_ == 1 && titre_ >= v(10)) || (titre_direction_ == -1 && titre_ <= v(0))) {
            titre_ = titre_ >= v(10) ? v(10) - physics::var_modulo(physics::var_abs(titre_), v(10)) : physics::var_modulo(physics::var_abs(titre_), v(10));
            titre_direction_ *= -1;
        }
        const V magnitude = physics::var_abs(turn_);
        const V percent = physics::var_clamp(magnitude - ((titre_ / v(2)) + titre_ * magnitude / v(200)) / v(2), v(0), v(100));
        pose(ctx, fast ? (turn_ > v(0) ? "hiz_sol" : "hiz_sag") : (turn_ > v(0) ? "yavas_sol" : "yavas_sag"), percent);
        i_ = v(100);
    };
    // Calls made after a source loop iteration execute the new coroutine before
    // its first wait(1), so transitions deliberately keep this same time value.
    while (s.ghost_reset_ok == v(0)) {
        if (phase_ == Phase::running && entry_ == 0 && !locomotion_ready_ && s.arac_ivme >= v(280)) {
            enter(ctx, Phase::slide, 1);
            continue;
        }
        if (phase_ == Phase::coast && entry_active_ && i_ == v(0) && entry_ != 3 && s.arac_ivme < v(40)) {
            enter(ctx, Phase::brake, entry_ == 1 ? 2 : 1, entry_ == 1 ? entry_turn_ : v(0));
            continue;
        }
        if (entry_active_) {
            const bool reverse_entry = phase_ == Phase::running && entry_ == 1;
            const bool finished = reverse_entry ? i_ < v(0) : i_ > v(100);
            if (!finished) {
                V yaw = v(0);
                if (phase_ != Phase::idle)
                    yaw = yaw_for(phase_ == Phase::coast ? v(10) : v(8), phase_ == Phase::coast ? v(4) : v(5),
                                  phase_ == Phase::coast || phase_ == Phase::brake);
                contact_.robot_ground_angles(ctx, time, yaw);
                V idle_collision = v(0);
                if (phase_ == Phase::idle) idle_collision = contact_.collision(ctx, time, true);
                limit_speed(v(0.5));
                if (phase_ == Phase::running)
                    s.arac_ivme = physics::var_min(s.arac_ivme + v(3.456) * time, s.arac_max_hiz);
                else
                    decelerate(phase_ == Phase::slide && entry_ != 3 ? v(0.576) : v(0.864));
                V lateral = v(0);
                if (phase_ == Phase::running) {
                    if (entry_ == 1) lateral = v(-15) * time;
                    if (entry_ == 3 || entry_ == 4) lateral = v(-10) * time;
                }
                if (phase_ == Phase::slide && entry_ == 1) lateral = v(15) * time;
                if (phase_ == Phase::brake && entry_ == 1) lateral = v(10) * time;
                if (phase_ == Phase::idle) contact_.gravity(ctx, time, idle_collision);
                else move(ctx, time, v(0), {0, physics::number(lateral), 0}, false, true);
                const V magnitude = physics::var_abs(entry_turn_);
                if (phase_ == Phase::idle) {
                    pose(ctx, "bekle_basla", i_);
                } else if (phase_ == Phase::running) {
                    if (entry_ == 1) pose(ctx, "hiz_kosma", i_);
                    else {
                        pose(ctx, entry_ == 2 ? "yavas_loop" : entry_ == 3 ? "fren_loop" : "bekle_basla", entry_ == 4 ? v(100) : v(0));
                        blend(ctx, "kosma", v(0), i_);
                    }
                } else if (phase_ == Phase::brake) {
                    pose(ctx, entry_ == 1 ? "kosma" : entry_ == 2 ? (entry_turn_ > v(0) ? "hiz_sol" : "hiz_sag") : (entry_turn_ > v(0) ? "yavas_sol" : "yavas_sag"), entry_ == 1 ? v(0) : magnitude);
                    blend(ctx, "fren_loop", v(0), i_);
                } else if (phase_ == Phase::coast) {
                    if (entry_ == 1 && entry_turn_ == v(0)) pose(ctx, "yavasla", i_);
                    else {
                        pose(ctx, entry_ == 1 ? (entry_turn_ > v(0) ? "hiz_sol" : "hiz_sag") : entry_ == 2 ? "kosma" : "fren_loop", entry_ == 1 ? magnitude : v(0));
                        blend(ctx, entry_turn_ == v(0) ? "yavas_loop" : entry_turn_ > v(0) ? "yavas_sol" : "yavas_sag", magnitude, i_);
                    }
                } else {
                    if (entry_ == 1) pose(ctx, "hiz_kosma", i_);
                    else if (entry_ == 2 && entry_turn_ == v(0)) pose(ctx, "yavasla", v(100) - i_);
                    else {
                        pose(ctx, entry_ == 2 ? (entry_turn_ > v(0) ? "yavas_sol" : "yavas_sag") : "fren_loop", entry_ == 2 ? magnitude : v(0));
                        blend(ctx, entry_turn_ == v(0) ? "hiz_loop" : entry_turn_ > v(0) ? "hiz_sol" : "hiz_sag", magnitude, i_);
                    }
                }
                const V duration = (phase_ == Phase::running && entry_ == 1) || (phase_ == Phase::slide && entry_ == 1) || ((phase_ == Phase::coast || phase_ == Phase::brake) && entry_ == 3) ? v(0.2) : v(0.3);
                const V increment = v(6.25) / duration * time;
                if (reverse_entry) i_ -= increment;
                else i_ += increment;
                if (active && input.key_d) entry_d_latched_ = true;
                contact_.shadows(ctx);
                if (phase_ != Phase::idle && !(phase_ == Phase::running && entry_ == 4)) skid(ctx);
                if (phase_ != Phase::idle && !(phase_ == Phase::running && entry_ == 4) &&
                    s.toprak_yol_ok == v(1) && s.c4_eski_havazaman < v(1)) {
                    dust(ctx, 0);
                    dust(ctx, 1);
                }
                ghost_run(ctx, time);
                return;
            }
            entry_active_ = false;
        }
        if (!locomotion_ready_) {
            locomotion_ready_ = true;
            if (phase_ == Phase::running) {
                if (entry_ >= 1 && entry_ <= 4) {
                    pose(ctx, entry_ == 1 ? "hiz_kosma" : "kosma", v(0));
                    if (entry_d_latched_) { start_reverse(0, v(0)); return; }
                }
                s.r_kayma_fark = v(0);
                first_cycle_ = true;
                i_ = v(100);
                step_ = 0;
                camera_stop_ = false;
                if (entry_ == 5) {
                    i_ = v(47.368);
                    step_ = 1;
                    first_cycle_ = s.arac_ivme < v(120);
                    camera_stop_ = true;
                }
                if (jump_held()) { start_jump(1, i_); return; }
                stop_skid(ctx);
            } else if (phase_ == Phase::idle) {
                pose(ctx, "bekle_loop", v(0));
                if (entry_d_latched_) { start_reverse(1, v(0)); return; }
                if (jump_held()) { start_jump(5, i_); return; }
                if (forward) { enter(ctx, Phase::running, 4); continue; }
                i_ = v(0);
            } else if (phase_ == Phase::brake) {
                turn_ = entry_turn_;
                pose(ctx, "fren_loop", v(0));
                if (entry_d_latched_) { start_reverse(2, v(0)); return; }
                if (jump_held()) { start_jump(4, i_); return; }
                if (forward) { enter(ctx, s.arac_ivme < v(240) ? Phase::running : Phase::slide, 3, turn_); continue; }
                if (s.arac_ivme == v(0)) { enter(ctx, Phase::idle); continue; }
                i_ = v(0);
            } else if (phase_ == Phase::coast) {
                turn_ = entry_turn_;
                pose(ctx, turn_ > v(0) ? "yavas_sol" : turn_ < v(0) ? "yavas_sag" : entry_ == 1 ? "yavasla" : "yavas_loop",
                     turn_ != v(0) ? physics::var_abs(turn_) : entry_ == 1 ? v(100) : v(0));
                if (entry_d_latched_) { start_reverse(3, transition_percent(turn_)); return; }
                if (jump_held()) { start_jump(3, v(0)); return; }
                if (forward) {
                    if (s.arac_ivme < v(240)) recenter_ = true;
                    else { enter(ctx, Phase::slide, 2, turn_); continue; }
                }
                if (backward || s.arac_ivme < v(40)) { enter(ctx, Phase::brake, 3, turn_); continue; }
                i_ = v(0);
            } else if (phase_ == Phase::slide) {
                if (entry_ != 1) turn_ = entry_turn_;
                pose(ctx, entry_ == 1 ? "hiz_kosma" : turn_ > v(0) ? "hiz_sol" : turn_ < v(0) ? "hiz_sag" : entry_ == 2 ? "yavasla" : "hiz_loop",
                     entry_ == 1 ? v(100) : physics::var_abs(turn_));
                if (entry_d_latched_) { start_reverse(0, v(0)); return; }
                if (jump_held()) { start_jump(2, v(0)); return; }
                if (input.key_cuu && s.arac_ivme < v(240)) {
                    if (turn_ == v(0)) { enter(ctx, Phase::running, 1); continue; }
                    recenter_ = true;
                }
                if (backward) { enter(ctx, Phase::brake, 2, turn_); continue; }
                if (released) coast_wait_ += v(6.25) / v(0.2) * time;
                i_ = v(0);
            }
        }
        if (phase_ == Phase::idle) {
            contact_.robot_ground_angles(ctx, time, v(0));
            const V collision = contact_.collision(ctx, time, true);
            limit_speed(v(0.2));
            contact_.gravity(ctx, time, collision);
            pose(ctx, "bekle_loop", i_, true);
            i_ += v(6.25) / v(2) * time;
            if (active && input.key_d) { start_reverse(1, v(0)); return; }
            if (jump_held()) { start_jump(5, i_); return; }
            cooldown();
            if (forward) { enter(ctx, Phase::running, 4); continue; }
            i_ = physics::var_modulo(i_, v(100));
            shadows_ghost(false);
            return;
        }
        if (phase_ == Phase::running) {
            static const std::array<V,20> run_side{v(0),v(0.326),v(-0.295),v(-3.486),v(-6.643),v(-3.062),v(-1.664),v(-1.532),v(-1.783),v(-1.077),v(-0.565),v(0.117),v(1.318),v(2.634),v(5.744),v(4.050),v(2.165),v(2.008),v(0.963),v(0.782)};
            static const std::array<V,20> start_forward{v(0),v(38.318),v(39.504),v(32.119),v(34.812),v(35.809),v(33.069),v(32.581),v(34.634),v(36.714),v(56.262),v(45.646),v(44.63),v(43.745),v(42.73),v(46.181),v(43.018),v(41.913),v(37.298),v(35.865)};
            static const std::array<V,20> start_side{v(0),v(-7.5),v(-9.297),v(-9.568),v(-8.609),v(-6.777),v(-6.411),v(-4.776),v(-2.032),v(-1.633),v(9.925),v(7.222),v(7.673),v(6.461),v(6.218),v(5.96),v(4.205),v(3.246),v(3.489),v(2.243)};
            if (i_ >= v(100)) {
                step_ = 0;
                camera_stop_ = false;
                s.r_kayma_fark = v(0);
                first_cycle_ = s.arac_ivme < v(120);
            }
            i_ = physics::var_modulo(i_, v(100));
            footsteps(ctx, i_);
            const V yaw = yaw_for(v(8), v(5), false);
            if (yaw != v(0)) {
                const V amount = v(6.25) / v(0.4) * time;
                turn_ = physics::var_clamp(input.key_cul ? turn_ + amount : turn_ - amount, v(-100), v(100));
            }
            else turn_ = turn_ >= v(0) ? physics::var_max(v(0), turn_ - v(6.25) / v(0.6) * time) : physics::var_min(v(0), turn_ + v(6.25) / v(0.6) * time);
            contact_.robot_ground_angles(ctx, time, yaw);
            limit_speed(v(0.5));
            const auto sample = [&](const auto& values) {
                const V index_value = physics::var_integer(i_ * v(19) / v(100));
                const std::size_t index = static_cast<std::size_t>(index_value.raw / 1024);
                const V start = index_value * v(100) / v(19);
                const V end = (index_value + v(1)) * v(100) / v(19);
                return values[index] + (i_ - start) / (end - start) * (values[index + 1] - values[index]);
            };
            V lateral;
            if (!first_cycle_) {
                if (active && forward) s.arac_ivme += v(1.152) * time;
                s.arac_ivme = physics::var_min(s.arac_ivme, s.arac_max_hiz);
                lateral = (v(1) + v(0.555) * (physics::var_min(s.arac_ivme, v(280)) / v(280))) * sample(run_side) * time;
            } else {
                lateral = sample(start_side) * time * v(1.25);
                s.arac_ivme += sample(start_forward) * v(1.25) / v(0.689) * time * (v(0.25) - v(0.2) * physics::var_min(s.arac_ivme, v(90)) / v(90));
            }
            move(ctx, time, v(0), {0, physics::number(lateral), 0}, false, true);
            if (!first_cycle_ && !camera_stop_) s.r_kayma_fark += lateral;
            pose(ctx, "kosma", i_, true);
            i_ += v(6.25) / (v(1.4) - v(0.5) * (physics::var_min(s.arac_ivme, v(280)) / v(280))) * time;
            if (active && input.key_d) { start_reverse(0, v(0)); return; }
            if (jump_held()) { start_jump(1, i_); return; }
            cooldown();
            const bool boundary = i_ == v(0) || i_ >= v(100);
            if (input.key_cuu && s.arac_ivme >= v(280) && boundary) {
                s.r_kayma_fark = v(0);
                enter(ctx, Phase::slide, 1);
                continue;
            }
            if (backward && boundary) {
                s.r_kayma_fark = v(0);
                enter(ctx, Phase::brake, 1);
                continue;
            }
            if (released && boundary) {
                s.r_kayma_fark = v(0);
                enter(ctx, Phase::coast, 2, transition_percent(turn_));
                continue;
            }
            contact_.shadows(ctx);
            if (s.toprak_yol_ok == v(1) && s.c4_eski_havazaman < v(1)) {
                if (i_ < v(52.632)) dust(ctx, 0);
                if (i_ < v(10.526) || i_ > v(42.105)) dust(ctx, 1);
            }
            ghost_run(ctx, time);
            return;
        }
        if (phase_ == Phase::slide || phase_ == Phase::coast || phase_ == Phase::brake) {
            const bool fast = phase_ == Phase::slide;
            const bool braking = phase_ == Phase::brake;
            const V yaw = yaw_for(fast ? v(8) : v(10), fast ? v(5) : v(4), !fast);
            steering_pose(yaw, braking ? v(0.5) : v(0.2));
            contact_.robot_ground_angles(ctx, time, yaw);
            limit_speed(fast ? v(0.5) : v(0.2));
            decelerate(fast ? v(0.576) : braking ? v(4.608) : v(1.152));
            move(ctx, time, v(0), {}, false, true);
            if (braking) {
                pose(ctx, "fren_loop", i_, true);
                i_ += v(6.25) / v(0.10) * time * physics::var_min(s.arac_ivme, v(80)) / v(80);
            } else if (turn_ == v(0)) {
                pose(ctx, fast ? "hiz_loop" : "yavas_loop", i_, true);
                i_ += v(6.25) / (fast ? v(0.05) : v(0.10)) * time;
            } else turned_animation(fast);
            if (active && input.key_d) {
                start_reverse(fast ? 0 : braking ? 2 : 3, !fast && !braking ? transition_percent(turn_) : v(0));
                return;
            }
            if (jump_held()) { start_jump(fast ? 2 : braking ? 4 : 3, i_); return; }
            cooldown();
            if (braking) {
                if (forward) {
                    enter(ctx, s.arac_ivme < v(240) ? Phase::running : Phase::slide, 3, s.arac_ivme < v(240) ? v(0) : transition_percent(turn_));
                    continue;
                }
                if (released && s.arac_ivme > v(50)) {
                    if (coast_wait_ > v(100)) { enter(ctx, Phase::coast, 3, transition_percent(turn_)); continue; }
                    coast_wait_ += v(6.25) / v(0.2) * time;
                } else coast_wait_ = v(0);
                if (s.arac_ivme == v(0)) { enter(ctx, Phase::idle); continue; }
            } else if (fast) {
                if (input.key_cuu && s.arac_ivme < v(240)) {
                    if (turn_ == v(0)) { enter(ctx, Phase::running, 1); continue; }
                    recenter_ = true;
                } else recenter_ = false;
                if (backward) { enter(ctx, Phase::brake, 2, transition_percent(turn_)); continue; }
                if (released) {
                    if (coast_wait_ > v(100)) { enter(ctx, Phase::coast, 1, transition_percent(turn_)); continue; }
                    coast_wait_ += v(6.25) / v(0.2) * time;
                } else coast_wait_ = v(0);
            } else {
                if (forward) {
                    if (s.arac_ivme < v(240)) {
                        if (turn_ == v(0)) { enter(ctx, Phase::running, 2); continue; }
                        recenter_ = true;
                    } else { enter(ctx, Phase::slide, 2, transition_percent(turn_)); continue; }
                } else {
                    if (backward || s.arac_ivme < v(40)) { enter(ctx, Phase::brake, 3, transition_percent(turn_)); continue; }
                    recenter_ = false;
                }
            }
            i_ = physics::var_modulo(i_, v(100));
            contact_.shadows(ctx);
            skid(ctx);
            if (s.toprak_yol_ok == v(1) && s.c4_eski_havazaman < v(1)) {
                dust(ctx, 0);
                dust(ctx, 1);
            }
            ghost_run(ctx, time);
            return;
        }
        return;
    }
}

} // namespace c4::game
