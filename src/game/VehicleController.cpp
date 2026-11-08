#include "VehicleController.hpp"
#include "physics/NativeNumeric.hpp"

namespace c4::game {
namespace {
using physics::v;
using physics::number;
using physics::body_source;
using physics::property;
using physics::native;
using physics::var_abs;
using physics::var_min;
using physics::var_max;
using physics::var_clamp;
using physics::var_sign;
using physics::var_ang;
using physics::var_sin;
using physics::var_cos;
using physics::var_sqrt;
using physics::var_modulo;
constexpr auto move_mode=physics::Mode::ignore_passable|physics::Mode::ignore_content|physics::Mode::ignore_push|physics::Mode::ignore_sprites|physics::Mode::glide;
std::array<EntityId,10> outer(const ComponentIds& i) { return {i.p_arac_cam,i.p_far_govde,i.p_far_cam,i.p_arac_ontakim,i.p_arac_koltuk,i.p_arac_direx,i.p_arac_kaporta,i.p_arac_ayna,i.p_arac_arkafar,i.p_arac_arkaglow}; }
std::array<EntityId,4> wheels(const ComponentIds& i) { return {i.p_fsolteker,i.p_fsagteker,i.p_bsolteker,i.p_bsagteker}; }
}

void VehicleController::SteeringRamp::update(bool held,Var time,Var rise,Var fall) {
    if(held) {
        if(on==v(0)) origin=value;
        off=v(0); on+=v(6.25)/(rise/v(1000))*time; value=origin+on;
    } else {
        if(off==v(0)) origin=value;
        on=v(0); off-=v(6.25)/(fall/v(1000))*time; value=origin-var_abs(off);
    }
    value=var_clamp(value,v(0),v(100));
}
void VehicleController::animate_parts(GameContext& c,std::string_view clip,Var frame,bool cycle,bool include_steering) {
    for(auto id:outer(c.ids)) {
        if(!include_steering && id==c.ids.p_arac_direx) continue;
        c.animate(id,clip,frame,cycle);
    }
}
void VehicleController::initialize(GameContext& c) {
    left_={}; right_={}; handbrake_=throttle_release_=slip_=reverse_wait_=v(0); forward_wait_=v(100);
    front_steer_=wheel_frame_=skid_frame_=startup_frame_=startup_cycle_=v(0); enter_ok_=v(0);
    motion_.reset();
    pivot_correction_={};
    auto& s=c.state;
    animate_parts(c,"inis",v(100));
    constexpr std::array<unsigned,4> vertices{4,3,2,1};
    auto ws=wheels(c.ids);
    // Engine-query outputs already contain original Q10 property words. Preserve them
    // without float ABI narrowing; this does not establish native geometry parity.
    for(std::size_t n=0;n<ws.size();++n) c.physics.body(ws[n]).position=native(body_source(c.physics.vec_for_vertex(c.ids.p_aracgovde,vertices[n])));
    s.c4_leftright_yon=v(0); s.arac_gidis_pan=property(c.physics.body(c.ids.p_aracgovde).angles.x);
    s.arac_turbo=v(1); nitro_started_=v(0); s.robot_action_no=v(0); s.robot_frame_no=v(0);
    if(s.arac_ivme==v(0))s.arac_vites_no=v(0);
    if(s.arac_ivme>=v(0) && s.arac_ivme<=v(53))s.arac_vites_no=v(1);
    if(s.arac_ivme>v(53) && s.arac_ivme<=v(96))s.arac_vites_no=v(2);
    if(s.arac_ivme>v(96) && s.arac_ivme<=v(160))s.arac_vites_no=v(3);
    if(s.arac_ivme>v(160) && s.arac_ivme<=v(245))s.arac_vites_no=v(4);
    if(s.arac_ivme>v(245))s.arac_vites_no=v(5);
    s.c4_vites1_time=var_min(var_abs(s.arac_ivme),v(62.35)); s.c4_vites7_time=v(0);
}
void VehicleController::reset(GameContext& c) {
    if(c.audio.playing(c.state.turbo_ses_handle2))c.audio.stop(c.state.turbo_ses_handle2);
    animation_type_=-1; initialize(c);
}
void VehicleController::begin_animation(GameContext& c,int type,Var frame,Var speed) {
    if(animation_type_!=-1)return;
    animation_type_=type; animation_frame_=type==2?v(50):frame; animation_speed_=speed;
    animation_wait_=type==1?v(.1)*v(16):v(0); settling_dust_=v(0);
    if(type==0)c.state.model_robot_ok=v(2);
    if(type!=1)update_animation(c,last_time_);
}
void VehicleController::gear_animation(GameContext& c) { begin_animation(c,1,v(0),v(0)); }
void VehicleController::start_settling(GameContext& c,Var frame,Var speed) {
    begin_animation(c,0,frame,speed);
}
void VehicleController::update_animation(GameContext& c,Var time) {
    if(animation_type_<0)return;
    if(animation_wait_>v(0)) { animation_wait_-=time; if(animation_wait_>v(0))return; }
    if(animation_type_==0) {
        if(animation_frame_<=v(110) && c.state.model_robot_ok==v(2) && !c.state.ghost_reset_ok) {
            if(animation_frame_>v(27) && settling_dust_==v(0)) {
                if(c.state.toprak_yol_ok==v(1) && c.state.c4_eski_havazaman<v(1))
                    c.effect("daire_toz_cik",c.physics.body(c.ids.p_aracgovde).position,{},c.ids.p_aracgovde,v(1));
                settling_dust_=v(1);
            }
            animate_parts(c,"inis",var_min(animation_frame_,v(100)));
            c.animate(c.ids.p_arac_koltuk,"inis",animation_frame_/v(1.1));
            animation_frame_+=v(6.25)/v(1.833)*time*v(3.5)*animation_speed_;
            return;
        }
        if(c.state.model_robot_ok==v(2)) { animate_parts(c,"inis",v(100),false,false); c.state.model_robot_ok=v(0); }
    } else if(animation_type_==1) {
        if(animation_frame_<=v(100) && !c.state.ghost_reset_ok) {
            animate_parts(c,"vites_at",animation_frame_); animation_frame_+=v(6.25)/v(.35)*time; return;
        }
        animate_parts(c,"vites_at",v(100));
    } else {
        if(animation_frame_<=v(450) && !c.state.ghost_reset_ok) {
            bool forward=animation_frame_<=v(100) || (animation_frame_>v(200) && animation_frame_<=v(300)) || animation_frame_>v(400);
            const Var frame=var_modulo(animation_frame_,v(100));
            animate_parts(c,"sagasola_yat",forward?frame:v(100)-frame);
            animation_frame_+=v(6.25)/(v(.05)+v(.15)*(animation_frame_-v(50))/v(400))*time; return;
        }
        animate_parts(c,"sagasola_yat",v(50));
    }
    animation_type_=-1;
}

void VehicleController::update(GameContext& c,const InputSnapshot& in,const FrameTiming& timing,bool advance_animation,bool advance_motion) {
    auto& s=c.state;
    if(s.freeze_mode>v(0) || s.ghost_reset_ok)return;
    const Var t=timing.originaltime_step_ticks;
    last_time_=t;
    if(advance_animation)update_animation(c,t);
    if(!advance_motion || s.arac_durum_tip!=v(0))return;
    if(in.key_d && enter_ok_==v(1) && s.ghost_yaris_start && !s.game_over_ok) {
        s.camera_tip_no=v(1); s.arac_durum_tip=v(1); s.model_robot_ok=v(1); s.arac_turbo=v(1);
        for(auto id:{c.ids.p_robot_teker,c.ids.p_robot_mekanik,c.ids.p_robot_farcam,c.ids.p_robot_fargovde,c.ids.p_robot_camlar,c.ids.p_robot_kaporta}) {
            c.copy_pose(id,c.ids.p_aracgovde); c.visuals[id].invisible=false;
        }
        for(auto id:outer(c.ids))c.visuals[id].invisible=true;
        for(auto id:wheels(c.ids))c.visuals[id].invisible=true;
        c.visuals[c.ids.p_arac_tekerler].invisible=true;
        if(c.audio.playing(c.state.turbo_ses_handle2))c.audio.stop(c.state.turbo_ses_handle2);
        s.phase_request=PhaseRequest::robot;
        return;
    }
    enter_ok_=v(1);
    const bool eligible=s.arac_ivme>=v(0) || (s.arac_ivme<v(0) && in.key_cud);
    left_.update(in.key_cul && eligible,t,s.fe_donmeivme_time,s.fe_toparlaivme_time);
    right_.update(in.key_cur && eligible,t,s.fe_donmeivme_time,s.fe_toparlaivme_time);
    s.c4_leftright_yon=left_.value-right_.value;
    auto& body=c.physics.body(c.ids.p_aracgovde);
    if(s.c4_eski_havazaman<v(1)) {
        const Var yaw_speed=s.fe_max_donmehiz*(s.c4_leftright_yon/v(100))*(v(1)-s.c4_eski_havazaman);
        Var yaw;
        // Keep the four source cases: moving a division or the reverse sign changes fixed arithmetic.
        if(s.arac_ivme>=v(-40) && s.arac_ivme<v(0))yaw=yaw_speed*(s.arac_ivme/v(40))*t;
        if(s.arac_ivme<v(-40))yaw=v(-1)*(yaw_speed-yaw_speed*(s.fe_hiz_donmeazal/v(100))*(var_abs(s.arac_ivme)-v(40))/v(400))*t;
        if(s.arac_ivme>=v(0) && s.arac_ivme<=v(40))yaw=yaw_speed*(s.arac_ivme/v(40))*t*(v(100)+handbrake_/v(4))/v(100);
        if(s.arac_ivme>v(40))yaw=(yaw_speed-yaw_speed*(s.fe_hiz_donmeazal/v(100))*(s.arac_ivme-v(40))/v(400))*t*(v(100)+handbrake_/v(2))/v(100);
        const unsigned vertex=yaw>v(0)?2:1;
        const auto old=body_source(c.physics.vec_for_vertex(c.ids.p_aracgovde,vertex));
        c.physics.rotate(c.ids.p_aracgovde,{number(yaw),0,0},physics::Mode::ignore_passable);
        const auto now=body_source(c.physics.vec_for_vertex(c.ids.p_aracgovde,vertex));
        motion_.remember_angle_scratch(now);
        const Var pivot_scale=(v(100)-handbrake_)/v(100);
        pivot_correction_={(old.x-now.x)*pivot_scale,(old.y-now.y)*pivot_scale,(old.z-now.z)*pivot_scale};
        if(s.ghost_yaris_start) {
            if(in.key_cuu && !s.game_over_ok) {
                Var grip=v(100);
                if(s.c4_leftright_yon!=v(0))grip=v(100)-var_abs(s.c4_leftright_yon)*(s.fe_yol_tutusmik/v(1.213))*(var_max(s.arac_ivme,v(160))-v(160))/(v(100)+((s.arac_turbo+s.start_turbo)-v(1))*v(100));
                const Var multiplier=(v(1)-s.c4_eski_havazaman)*(s.arac_turbo+s.start_turbo)*v(.8)*grip/v(100);
                // Source descending independent ifs: retain mutation between each predicate.
                if(s.arac_ivme>v(350))s.arac_ivme+=v(.793)*t*multiplier;
                static const std::array<Var,5> low{v(273),v(200),v(136),v(86),v(0)}, high{v(350),v(273),v(200),v(136),v(86)}, width{v(77),v(73),v(64),v(50),v(86)}, ratio{v(1),v(1.312),v(1.685),v(2.360),v(3.827)};
                for(std::size_t n=0;n<low.size();++n) {
                    const Var transition=n==4?width[n]*v(.2):low[n]+width[n]*v(.2);
                    if(s.arac_ivme>transition && s.arac_ivme<=high[n])s.arac_ivme+=(ratio[n]-ratio[n]*v(.6)*((s.arac_ivme-transition)/(high[n]-transition)))*t*multiplier;
                    if((n==4?s.arac_ivme>=low[n]:s.arac_ivme>low[n]) && s.arac_ivme<=transition) {
                        if(n!=4 || forward_wait_>=v(100)) {
                            if(n==4)reverse_wait_=v(0);
                            s.arac_ivme+=ratio[n]*t*multiplier;
                        } else forward_wait_+=v(6.25)/v(.25)*t;
                    }
                }
                if(s.arac_ivme<v(0)) {
                    c.visuals[c.ids.p_arac_arkafar].skin=1;
                    c.visuals[c.ids.p_arac_arkafar].invisible=c.visuals[c.ids.p_arac_arkaglow].invisible=false;
                    s.arac_ivme=var_min(s.arac_ivme+v(6.28)*t*multiplier,v(0));
                } else c.visuals[c.ids.p_arac_arkafar].invisible=c.visuals[c.ids.p_arac_arkaglow].invisible=true;
                s.arac_ivme=var_min(s.arac_ivme,s.arac_max_hiz);
                throttle_release_=var_max(throttle_release_-v(6.25)/v(.5)*t,v(0));
            } else if(!in.key_cud || s.game_over_ok) {
                if(s.arac_ivme!=v(0))c.visuals[c.ids.p_arac_arkafar].invisible=c.visuals[c.ids.p_arac_arkaglow].invisible=true;
                // The source multiplies each coefficient by time BEFORE the airborne factor.
                if(s.arac_ivme>v(0)) {
                    if(s.arac_ivme<=v(40))s.arac_ivme-=v(1.44)*t*(v(1)-s.c4_eski_havazaman);
                    if(s.arac_ivme>v(40) && s.arac_ivme<=v(60))s.arac_ivme-=v(1.543)*t*(v(1)-s.c4_eski_havazaman);
                    if(s.arac_ivme>v(60) && s.arac_ivme<=v(90))s.arac_ivme-=v(1.61)*t*(v(1)-s.c4_eski_havazaman);
                    if(s.arac_ivme>v(90) && s.arac_ivme<=v(140))s.arac_ivme-=v(1.78)*t*(v(1)-s.c4_eski_havazaman);
                    if(s.arac_ivme>v(140) && s.arac_ivme<=v(240))s.arac_ivme-=v(1.856)*t*(v(1)-s.c4_eski_havazaman);
                    if(s.arac_ivme>v(240))s.arac_ivme-=v(2.032)*t*(v(1)-s.c4_eski_havazaman);
                    s.arac_ivme=var_max(s.arac_ivme,v(0));
                } else s.arac_ivme=var_min(s.arac_ivme+v(1.44)*t*(v(1)-s.c4_eski_havazaman),v(0));
                throttle_release_=var_min(throttle_release_+v(6.25)/v(1)*t,v(100));
            }
            if(in.key_cud && !in.key_cuu && !s.game_over_ok) {
                const Var multiplier=(v(1)-s.c4_eski_havazaman)/v(1);
                if(s.arac_ivme>v(0)) {
                    c.visuals[c.ids.p_arac_arkafar].skin=1;
                    c.visuals[c.ids.p_arac_arkafar].invisible=c.visuals[c.ids.p_arac_arkaglow].invisible=false;
                    s.arac_ivme=var_max(s.arac_ivme-v(6.28)*t*multiplier,v(0));
                } else if(reverse_wait_>=v(100)) {
                    c.visuals[c.ids.p_arac_arkafar].skin=2; c.visuals[c.ids.p_arac_arkafar].invisible=false; c.visuals[c.ids.p_arac_arkaglow].invisible=true;
                    s.arac_ivme=var_max(s.arac_ivme-v(3)*t*multiplier,v(-107)); forward_wait_=v(0);
                } else {
                    c.visuals[c.ids.p_arac_arkafar].invisible=c.visuals[c.ids.p_arac_arkaglow].invisible=true; reverse_wait_+=v(6.25)/v(.25)*t;
                }
                throttle_release_=var_max(throttle_release_-v(6.25)/v(.5)*t,v(0));
            }
            if(in.key_space || s.game_over_ok) {
                handbrake_=var_min(handbrake_+v(6.25)/v(.1)*t,v(100));
                const Var old_sign=var_sign(s.arac_ivme);
                s.arac_ivme-=v(4.76)*t*(v(1)-s.c4_eski_havazaman)*old_sign;
                if(old_sign!=var_sign(s.arac_ivme))s.arac_ivme=v(0);
            } else handbrake_=var_max(handbrake_-v(6.25)/v(.1)*t,v(0));
        }
        s.arac_gidis_tilt=property(body.angles.y);
    } else {
        handbrake_=var_max(handbrake_-v(6.25)/v(.1)*t,v(0)); motion_.airborne_angles(c,t);
    }
    if(s.arac_pati_aktif)slip_=v(100); else slip_=var_max(slip_-v(6.25)/v(2)*t,v(0));
    const Var previous=s.arac_gidis_pan, gap=var_ang(property(body.angles.x)-s.arac_gidis_pan);
    s.arac_gidis_pan+=(v(.35)+v(.65)*(v(1)-slip_/v(100)))*(s.fe_yol_tutusmik-s.fe_yol_tutusmik*(s.fe_hiz_tutusazal/v(100))*(var_max(s.arac_ivme,v(40))-v(40))/v(400)+(s.fe_hiz_gazbirak/v(1000))*throttle_release_/v(100))*var_sqrt(var_abs(gap))*var_sign(gap)*t;
    if(var_sign(var_ang(property(body.angles.x)-s.arac_gidis_pan))!=var_sign(var_ang(property(body.angles.x)-previous)))s.arac_gidis_pan=property(body.angles.x);
    const Var collision=motion_.collision(c,t);
    // Source component formulas are scalar operations, not native vec_rotate/vec_scale.
    s.arac_cekimkuvveti={v(.689)*s.arac_ivme*t*var_cos(s.arac_gidis_pan)*var_cos(s.arac_gidis_tilt)+pivot_correction_.x,
                         v(.689)*s.arac_ivme*t*var_sin(s.arac_gidis_pan)*var_cos(s.arac_gidis_tilt)+pivot_correction_.y,
                         v(.689)*s.arac_ivme*t*var_sin(s.arac_gidis_tilt)+pivot_correction_.z};
    // MoveResult.distance is already an original API Q10 word, not a float ABI argument.
    s.body_skill2=property(c.physics.move(c.ids.p_aracgovde,{},native(s.arac_cekimkuvveti),move_mode).distance);
    motion_.gravity(c,t,collision);
    if(collision==v(1))c.effect("kivilci_cik_fx",native(s.crash_target),{},c.ids.p_aracgovde);
    motion_.ground_angles(c,t);
    if(motion_.consume_landing_animation())begin_animation(c,2,v(0),v(0));
    publish_pose(c,in,t);
    if(s.turbo_ekle_miktar>v(0) && in.key_cuu && in.key_s && s.arac_ivme>v(50) && !s.game_over_ok) {
        s.arac_turbo=v(5); s.arac_max_hiz=var_min(s.arac_max_hiz+v(6.25)/v(.4)*t,v(440)); s.turbo_ekle_miktar=var_max(s.turbo_ekle_miktar-v(6.25)/v(2.5)*t,v(0));
        if(nitro_started_==v(0)) { if(c.audio.playing(s.turbo_ses_handle1))c.audio.stop(s.turbo_ses_handle1); s.turbo_ses_handle1=c.sound("nitro_start_wav",v(100)); nitro_started_=v(1); }
        if(!c.audio.playing(s.turbo_ses_handle2))s.turbo_ses_handle2=c.sound("nitro_loop_wav",v(80),true);
    } else {
        if(c.audio.playing(c.state.turbo_ses_handle2))c.audio.stop(c.state.turbo_ses_handle2);
        nitro_started_=v(0); s.arac_turbo=v(1);
        s.arac_max_hiz=var_max(s.arac_max_hiz-v(6.25)/(s.arac_ivme>v(350)?v(4):v(.4))*t,v(350));
    }
    gears(c,in,t);
}

void VehicleController::publish_pose(GameContext& c,const InputSnapshot& in,Var time) {
    auto& s=c.state; auto& b=c.physics.body(c.ids.p_aracgovde);
    c.copy_pose(c.ids.p_camera_target,c.ids.p_aracgovde); c.physics.body(c.ids.p_camera_target).angles.x=number(s.arac_gidis_pan);
    for(auto id:outer(c.ids))c.copy_pose(id,c.ids.p_aracgovde);
    c.copy_pose(c.ids.p_arac_tekerler,c.ids.p_aracgovde);
    for(auto id:{c.ids.p_ic_camlar,c.ids.p_ic_panel,c.ids.p_ic_tavan,c.ids.p_ic_simit,c.ids.p_ic_rakam0,c.ids.p_ic_rakam1,c.ids.p_ic_rakam2,c.ids.p_ic_turbo})c.copy_pose(id,c.ids.p_aracgovde);
    if((in.key_cul || in.key_cur) && !s.game_over_ok) {
        Var change=(v(10)-v(5)*s.arac_ivme/v(340))*(v(in.key_cul)-v(in.key_cur))*time;
        if(var_sign(change)!=var_sign(front_steer_) && front_steer_!=v(0))change+=change;
        front_steer_+=change;
        const Var limit=v(40)-v(16)*(s.arac_ivme/v(340));
        if(var_abs(front_steer_)>limit)front_steer_=limit*var_sign(front_steer_);
    } else if(front_steer_!=v(0)) {
        const Var previous=front_steer_; front_steer_+=v(-8)*var_sign(front_steer_)*time;
        if(var_sign(previous)!=var_sign(front_steer_))front_steer_=v(0);
    }
    motion_.shadows(c);
    c.animate(c.ids.p_arac_direx,"dondur",v(50)-front_steer_*v(1.25));
    c.animate(c.ids.p_ic_simit,"dondur",(s.c4_leftright_yon+v(100))/v(2));
    if(animation_type_<0)animate_parts(c,"sagasola_yat",((var_clamp(s.c4_leftright_yon*v(100),v(-7000),v(7000))/v(70))*(var_clamp(var_abs(s.arac_ivme),v(50),v(200))-v(50))/v(150)+v(100))/v(2),false,false);
    const auto ws=wheels(c.ids); constexpr std::array<unsigned,4> vs{4,3,2,1};
    wheel_frame_=var_modulo(wheel_frame_+var_min(s.body_skill2*v(65)/v(81.053),v(10))*var_sign(s.arac_ivme),v(100));
    if(s.arac_pati_aktif)skid_frame_=var_modulo(skid_frame_+v(6.25)/v(1)*time,v(100)); else skid_frame_=v(0);
    for(std::size_t n=0;n<ws.size();++n) {
        auto& wheel=c.physics.body(ws[n]); wheel.position=native(body_source(c.physics.vec_for_vertex(c.ids.p_aracgovde,vs[n]))); wheel.angles=b.angles;
        if(n<2)wheel.angles.x=number(property(b.angles.x)+front_steer_);
        c.animate(ws[n],"frame",n<2?wheel_frame_:var_modulo(wheel_frame_+skid_frame_,v(100)),true);
    }
}

void VehicleController::gears(GameContext& c,const InputSnapshot& in,Var time) {
    auto& s=c.state; Var motor{};
    if(s.arac_ivme==v(0) && !in.key_cuu && !in.key_cud) {
        c.visuals[c.ids.p_arac_arkafar].skin=1; c.visuals[c.ids.p_arac_arkafar].invisible=c.visuals[c.ids.p_arac_arkaglow].invisible=false;
        s.arac_vites_no=v(0); s.c4_vites7_time=v(0); if(s.ghost_yaris_start)s.c4_vites1_time=v(0);
    }
    if(in.key_cuu && !s.game_over_ok) {
        if(s.ghost_yaris_start) {
            if((s.arac_ivme>=v(0) && s.arac_ivme<=v(86) && s.arac_vites_no!=v(2))||(s.arac_ivme>=v(0) && s.arac_ivme<=v(64) && s.arac_vites_no==v(2)))s.arac_vites_no=v(1);
            if((s.arac_ivme>v(86) && s.arac_ivme<=v(136) && s.arac_vites_no!=v(3))||(s.arac_ivme>v(64) && s.arac_ivme<=v(109) && s.arac_vites_no==v(3)))s.arac_vites_no=v(2);
            if((s.arac_ivme>v(136) && s.arac_ivme<=v(200) && s.arac_vites_no!=v(4))||(s.arac_ivme>v(109) && s.arac_ivme<=v(167) && s.arac_vites_no==v(4)))s.arac_vites_no=v(3);
            if((s.arac_ivme>v(200) && s.arac_ivme<=v(273) && s.arac_vites_no!=v(5))||(s.arac_ivme>v(167) && s.arac_ivme<=v(234) && s.arac_vites_no==v(5)))s.arac_vites_no=v(4);
            if((s.arac_ivme>v(273) && s.arac_vites_no!=v(5))||(s.arac_ivme>v(234) && s.arac_vites_no==v(5)))s.arac_vites_no=v(5);
            s.c4_vites1_time+=v(6.25)/v(.6)*time;
        } else s.c4_vites1_time+=v(6.25)/(v(.05)+v(.05)*(s.arac_vites_ivme/v(100))*(s.arac_vites_ivme/v(100)))*time;
        s.c4_vites1_time=var_min(s.c4_vites1_time,v(101.18));
    } else {
        if(s.ghost_yaris_start) {
            if(s.arac_ivme>v(0) && s.arac_ivme<=v(43) && s.arac_vites_no>=v(1))s.arac_vites_no=v(1);
            if(s.arac_ivme>v(43) && s.arac_ivme<=v(61) && s.arac_vites_no>=v(2))s.arac_vites_no=v(2);
            if(s.arac_ivme>v(61) && s.arac_ivme<=v(80) && s.arac_vites_no>=v(3))s.arac_vites_no=v(3);
            if(s.arac_ivme>v(80) && s.arac_ivme<=v(96) && s.arac_vites_no>=v(4))s.arac_vites_no=v(4);
            if(s.arac_ivme>v(96) && s.arac_vites_no==v(5))s.arac_vites_no=v(5);
            s.c4_vites1_time-=v(6.25)/v(.2)*time;
        } else s.c4_vites1_time-=v(6.25)/(v(.4)-v(.2)*(s.arac_vites_ivme/v(100)))*time;
        s.c4_vites1_time=var_max(s.c4_vites1_time,v(0));
    }
    if(in.key_cud && !in.key_cuu && !s.game_over_ok) {
        if(s.arac_ivme<=v(0) && s.ghost_yaris_start)s.arac_vites_no=v(7);
        s.c4_vites7_time=var_min(s.c4_vites7_time+v(6.25)/v(1.2)*time,v(107));
    } else {
        if(s.arac_ivme<v(0))s.arac_vites_no=v(7);
        s.c4_vites7_time=var_max(s.c4_vites7_time-v(6.25)/v(.2)*time,v(0));
    }
    if(s.arac_ivme<v(0))s.c4_vites1_time=v(0);
    if(s.arac_ivme>v(0))s.c4_vites7_time=v(0);
    static const std::array<Var,8> max_gears{v(0),v(86),v(136),v(200),v(273),v(350),v(0),v(107)};
    const auto gear=static_cast<std::size_t>(s.arac_vites_no.raw/1024);
    if(s.arac_vites_no!=v(0)) {
        if(s.arac_vites_no==v(7))motor=var_max(var_abs(s.c4_vites7_time),var_abs(s.arac_ivme))/v(107);
        else if(s.arac_vites_no==v(1) && s.c4_vites1_time*v(.85)>s.arac_ivme)motor=var_abs(s.c4_vites1_time)/v(101.18);
        else motor=var_min(var_abs(s.arac_ivme),v(350))/max_gears[gear];
        s.arac_vites_ivme=(s.arac_vites_no==v(1) && s.c4_vites1_time*v(.85)>s.arac_ivme?v(100):v(85))*var_sin(motor*v(30))*v(2);
        s.motor_ses_ivme=v(100)+v(100)*var_sin(motor*v(30))*v(2);
        s.motor_ses_volume=var_clamp(v(100)*(var_abs(s.arac_ivme)-max_gears[gear-1])/(max_gears[gear]-max_gears[gear-1]),v(0),v(100));
    } else if(!s.ghost_yaris_start) {
        motor=s.c4_vites1_time/v(101.18);
        s.arac_vites_ivme=v(100)*var_sin(motor*v(30))*v(2); s.motor_ses_ivme=v(100)+v(80)*var_sin(motor*v(30))*v(2); s.motor_ses_volume=var_clamp(v(100)*motor,v(0),v(100));
    } else { s.arac_vites_ivme=v(0); s.motor_ses_volume=v(0); s.motor_ses_ivme=v(50); }
    if(!s.ghost_yaris_start) {
        c.visuals[c.ids.p_arac_arkafar].invisible=c.visuals[c.ids.p_arac_arkaglow].invisible=var_sign(motor)!=v(0);
        if(motor<v(1)) { startup_frame_=motor*v(100); animate_parts(c,"start_ilkgaz",startup_frame_); startup_cycle_=v(0); }
        else { animate_parts(c,"start_loopgaz",startup_cycle_,true); startup_cycle_=var_modulo(startup_cycle_+v(38),v(100)); startup_frame_=v(100); }
    } else if(startup_frame_>v(0)) {
        if(animation_type_>=0)startup_frame_=v(0);
        else { startup_frame_=var_max(startup_frame_-v(6.25)/v(3.5)*time,v(0)); animate_parts(c,"start_ilkgaz",startup_frame_); }
    }
}
} // namespace c4::game
