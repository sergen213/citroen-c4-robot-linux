#include "SourceMotion.hpp"

#include "physics/NativeNumeric.hpp"

namespace c4::game {
namespace {
using physics::Var;
using physics::ScriptVec;
using physics::v;
using physics::number;
using physics::body_source;
using physics::property;
using physics::native;
using physics::var_abs;
using physics::var_sign;
using physics::var_min;
using physics::var_max;
using physics::var_clamp;
using physics::var_ang;
using physics::var_sin;
using physics::var_cos;
using physics::var_sqrt;
using physics::var_asin;
using physics::var_vec_scale;
using physics::var_vec_rotate;
using physics::var_vec_to_angle;
// Scalar-math parity is limited to the evidence documented by NativeNumeric;
// use its shared primitives, never a controller-specific trig convention.
ScriptVec vertex(GameContext& ctx,std::uint32_t index) {
    return body_source(ctx.physics.vec_for_vertex(ctx.ids.p_aracgovde,index,ctx.visuals.at(ctx.ids.p_aracgovde).sample));
}
constexpr auto contact_mode=physics::Mode::ignore_me|physics::Mode::ignore_passable|physics::Mode::ignore_flag2;
constexpr auto fallback_mode=physics::Mode::ignore_me|physics::Mode::ignore_passable;
Var pair_height(Var a,Var b) {
    const Var missing=v(-11111);
    Var result=missing;
    if(a.raw!=missing.raw && a.raw>=b.raw) result=a;
    if(b.raw!=missing.raw && b.raw>a.raw) result=b;
    return result;
}
Var angle_step(Var gap,Var step,Var proportional) {
    if(var_abs(step).raw>var_abs(proportional).raw) step=proportional;
    if(var_sign(gap).raw!=var_sign(var_ang(gap-step)).raw) step=gap;
    return step;
}
Var fallback(GameContext& ctx,Var time,Var rate,bool roll) {
    auto from=vertex(ctx,12);
    auto to=vertex(ctx,roll?10:11);
    to.z=to.z-v(0.1);
    if(ctx.physics.trace(native(from),native(to),fallback_mode,ctx.ids.p_aracgovde).trace_hit) return rate*time;
    from=vertex(ctx,roll?11:10);
    to=vertex(ctx,9);
    to.z=to.z-v(0.1);
    if(ctx.physics.trace(native(from),native(to),fallback_mode,ctx.ids.p_aracgovde).trace_hit) return v(0)-rate*time;
    return v(0);
}
void scrape(GameContext& ctx,bool robot) {
    auto& s=ctx.state;
    const Var gap=var_abs(var_ang(s.crash_hedef-property(ctx.physics.body(ctx.ids.p_aracgovde).angles.x)));
    if(!ctx.audio.playing(s.carp_loop_handle)) s.carp_loop_handle=ctx.sound("carp_surt_wav",v(robot?5:10),true);
    const Var volume=robot
        ? var_clamp((v(90)-gap)*v(0.5)*var_min(var_abs(s.arac_ivme)/v(2),v(100))/v(100),v(5),v(100))
        : var_clamp((v(90)-gap)*v(1.1)*var_min(var_abs(s.arac_ivme),v(100))/v(100),v(4),v(100));
    ctx.audio.tune(s.carp_loop_handle,static_cast<float>(number(volume)),0,0);
}
void low_impact(GameContext& ctx,Var volume) {
    constexpr std::array names{"carp_low1_wav","carp_low2_wav","carp_low3_wav"};
    auto& handle=ctx.state.carp_ses2_handle;
    if(!ctx.audio.playing(handle)) {
        const Var index=ctx.random(v(300))/v(100);
        handle=ctx.sound(names.at(static_cast<std::size_t>(index.raw/1024)),volume);
    }
}
}

void SourceMotion::reset() {
    gravity_return_=v(0);
    landing_airtime_=v(0);
    angle_scratch_={};
    landing_animation_=false;
    wheel_contacts_={};
}

void SourceMotion::bounce(GameContext& ctx,const physics::EntityEvent& event) {
    if(event.me!=ctx.ids.p_aracgovde || event.you==physics::no_entity) return;
    if(!ctx.physics.body(event.you).flags.flag2) return;
    const auto target=body_source(event.target);
    const auto normal=var_vec_to_angle(body_source(event.normal),ctx.state.crash_angle);
    ctx.state.crash_target={target.x,target.y,target.z};
    ctx.state.crash_angle={normal.x,normal.y,normal.z};
    ctx.state.collision_ok=v(1);
}

Var SourceMotion::collision(GameContext& ctx,Var time,bool robot) {
    auto& s=ctx.state;
    auto& body=ctx.physics.body(ctx.ids.p_aracgovde);
    const Var previous=s.collision_ok;
    if(s.collision_ok.raw==v(1).raw) {
        // Keep each original expression's left-to-right multiplication order.
        body.position.x=number(property(body.position.x)+v(5)*var_cos(s.crash_angle.x)*var_min(var_abs(s.arac_ivme),v(30))/v(30));
        body.position.y=number(property(body.position.y)+v(5)*var_sin(s.crash_angle.x)*var_min(var_abs(s.arac_ivme),v(30))/v(30));
        body.position.z=number(property(body.position.z)+v(5)*var_min(var_abs(s.arac_ivme),v(30))/v(30));
        s.crash_hedef=s.crash_angle.x-v(90)*var_sign(var_ang(s.crash_angle.x-property(body.angles.x)));
        const Var before=var_abs(s.arac_ivme);
        const Var gap=var_abs(var_ang(s.crash_hedef-property(body.angles.x)));
        s.arac_ivme=s.arac_ivme*(v(1)-var_min((v(1)-var_sin(v(90)-(robot?gap:var_max(gap,v(10)))))*var_sqrt(v(2)*time),v(1)));
        const Var loss=before-var_abs(s.arac_ivme);
        if(robot) {
            if(gap.raw>v(10).raw && loss.raw>v(25).raw && before.raw>v(60).raw) {
                s.bombacam_basla_m=(v(0.5)*gap/v(90))*(var_min(before-v(60),v(290))/v(290));
                s.bombacam_basla_z=v(0.5);
                s.bombacam_basla_t=v(1);
                low_impact(ctx,var_min(before,v(100)));
                scrape(ctx,true);
            } else if(before.raw>v(25).raw) {
                scrape(ctx,true);
            } else if(ctx.audio.playing(s.carp_loop_handle)) {
                ctx.audio.stop(s.carp_loop_handle);
            }
        } else {
            if(before.raw>v(50).raw && gap.raw>v(2).raw && (loss.raw>v(5).raw || (var_abs(s.arac_ivme)*v(1.111)).raw<before.raw)) {
                s.bombacam_basla_m=(v(0.5)*gap/v(90))*(var_min(before-v(50),v(300))/v(300));
                s.bombacam_basla_z=v(0.5);
                s.bombacam_basla_t=v(1);
            }
            if(gap.raw>v(7.5).raw && loss.raw>v(5).raw && before.raw>v(40).raw) {
                if(loss.raw>v(30).raw && gap.raw>v(20).raw) {
                    if(!ctx.audio.playing(s.carp_ses1_handle)) {
                        constexpr std::array names{"carp_high1_wav","carp_high2_wav","carp_high3_wav","carp_high4_wav"};
                        const Var index=ctx.random(v(400))/v(100);
                        s.carp_ses1_handle=ctx.sound(names.at(static_cast<std::size_t>(index.raw/1024)),
                                                   var_clamp(var_min(before,v(350))/v(2.5),v(1),v(100)));
                    }
                } else {
                    low_impact(ctx,var_clamp(before,v(1),v(100)));
                    scrape(ctx,false);
                }
            } else {
                scrape(ctx,false);
            }
            s.crash_miktar=var_ang(s.crash_angle.x-v(90)*var_sign(var_ang(s.crash_angle.x-property(body.angles.x)))-property(body.angles.x));
        }
        s.crash_totaltime=v(0);
        s.collision_ok=v(0);
    } else {
        s.crash_totaltime=var_min(s.crash_totaltime+v(6.25)/v(0.25)*time,v(100));
        if(s.crash_totaltime.raw==v(100).raw) s.crash_hedef=v(-666);
    }
    if(s.crash_hedef.raw!=v(-666).raw) {
        if(!robot) {
            const Var gap=var_ang(s.crash_hedef-property(body.angles.x));
            const Var amount=var_abs(gap);
            const Var gain=amount.raw>=v(60).raw?v(0):(amount.raw>=v(30).raw?(v(60)-amount)/v(30):amount/v(30));
            Var yaw=v(6)*time*(var_sin(amount)/v(4)+v(0.75))*gain*var_sign(gap);
            if(var_sign(var_ang(s.crash_hedef-(property(body.angles.x)+yaw))).raw!=var_sign(gap).raw) {
                yaw=gap;
                s.crash_hedef=v(-666);
            }
            ctx.physics.rotate(ctx.ids.p_aracgovde,{number(yaw),0,0},physics::Mode::ignore_passable);
            const Var before=s.arac_gidis_pan;
            s.arac_gidis_pan=s.arac_gidis_pan+(s.toprak_yol_ok.raw==0?v(0.05):v(0.02))*var_sign(property(body.angles.x)-s.arac_gidis_pan)*time;
            if(var_sign(var_ang(property(body.angles.x)-s.arac_gidis_pan)).raw!=var_sign(var_ang(property(body.angles.x)-before)).raw)
                s.arac_gidis_pan=property(body.angles.x);
        }
        // The source still tunes once when this iteration's yaw expires the tangent.
        scrape(ctx,robot);
    } else if(ctx.audio.playing(s.carp_loop_handle)) {
        ctx.audio.stop(s.carp_loop_handle);
    }
    return previous;
}

Var SourceMotion::gravity(GameContext& ctx,Var time,Var collision_result) {
    auto& s=ctx.state;
    auto& body=ctx.physics.body(ctx.ids.p_aracgovde);
    const auto from=body_source(body.position);
    auto end=from;
    end.z=end.z-v(1000);
    const auto mode=contact_mode|physics::Mode::ignore_content|physics::Mode::ignore_push|
                    physics::Mode::ignore_sprites|physics::Mode::use_box;
    const auto hit=ctx.physics.trace(native(from),native(end),mode,ctx.ids.p_aracgovde);
    const Var distance=property(hit.distance);
    s.yc_alt_mesafe=distance;
    if(hit.trace_hit) {
        const bool dirt=ctx.physics.body(hit.you).flags.flag4;
        s.fe_yol_tutusmik=v(dirt?1.1:1.441);
        s.fe_hiz_donmeazal=v(dirt?58.247:83.210);
        s.fe_hiz_tutusazal=v(dirt?80:71.836);
        s.toprak_yol_ok=v(dirt?1:0);
        if(distance.raw<0) {
            const Var penetration=distance-v(1);
            body.position.z=number(property(body.position.z)-penetration);
            s.c4_guncel_havazaman=v(0);
            s.c4_havadongu_no=v(0);
            // This early return deliberately preserves eski_havazaman/dusme_dist.
            gravity_return_=v(0)-penetration;
            return gravity_return_;
        }
    }
    if(s.c4_havadongu_no.raw==v(1).raw) {
        if(s.c4_guncel_havazaman.raw<v(1400).raw) s.c4_eski_havazaman=s.c4_guncel_havazaman;
    } else {
        s.c4_eski_havazaman=v(0);
    }
    if(!hit.trace_hit || distance.raw>v(1).raw) {
        if(s.c4_guncel_havazaman.raw<v(1400).raw) {
            s.c4_guncel_havazaman=s.c4_guncel_havazaman+(hit.trace_hit
                ? v(6.25)/v(1)*time*(v(1)+var_max(s.arac_ivme,v(350))/v(1050))+collision_result*v(6.25)/v(0.1)*time
                : v(6.25)/v(1)*time);
        }
        s.c4_havadongu_no=v(1);
    } else {
        s.c4_guncel_havazaman=v(0);
        s.c4_havadongu_no=v(0);
        s.c4_eski_havazaman=v(0);
    }
    const Var newer=(s.c4_guncel_havazaman+v(10))/v(100);
    const Var older=s.c4_eski_havazaman.raw!=0?(s.c4_eski_havazaman+v(10))/v(100):v(0);
    s.c4_dusme_dist=v(39.671)*v(0.5)*s.yercekimi_gucu*(newer*newer-older*older);
    if(hit.trace_hit && s.c4_dusme_dist.raw>=(distance-v(1)).raw) {
        s.c4_dusme_dist=distance-v(1);
        s.c4_guncel_havazaman=v(0);
        s.c4_havadongu_no=v(0);
        s.c4_eski_havazaman=v(0);
    } else if(!hit.trace_hit && s.c4_dusme_dist.raw>=v(1000).raw) {
        s.c4_dusme_dist=v(1000)-v(1);
    }
    body.position.z=number(property(body.position.z)-s.c4_dusme_dist);
    gravity_return_=v(0)-s.c4_dusme_dist;
    return gravity_return_;
}

void SourceMotion::airborne_angles(GameContext& ctx,Var time) {
    auto& s=ctx.state;
    auto& body=ctx.physics.body(ctx.ids.p_aracgovde);
    const ScriptVec force{s.arac_cekimkuvveti.x,s.arac_cekimkuvveti.y,gravity_return_+s.arac_cekimkuvveti.z};
    angle_scratch_=var_vec_to_angle(force,angle_scratch_);
    const Var tilt_gap=var_ang(angle_scratch_.y-property(body.angles.y));
    const Var roll_gap=var_ang(v(0)-property(body.angles.z));
    const Var speed=s.arac_ivme/s.arac_max_hiz;
    const Var tilt=angle_step(tilt_gap,v(4)*var_sign(tilt_gap)*time*speed,v(0.05)*tilt_gap*speed*var_sqrt(v(2)*time));
    const Var roll=angle_step(roll_gap,v(0.5)*var_sign(roll_gap)*time*speed,v(0.2)*roll_gap*var_sqrt(v(2)*time));
    ctx.physics.rotate(ctx.ids.p_aracgovde,{0,number(tilt),number(roll)},physics::Mode::ignore_passable);
    if(s.c4_eski_havazaman.raw>v(1).raw) s.c4_yere_carpma=s.c4_yere_carpma+v(6.25)/v(0.4)*time;
    s.c4_yere_carpma=var_min(s.c4_yere_carpma,v(1000));
    landing_airtime_=s.c4_eski_havazaman;
}

Var SourceMotion::contact_height(GameContext& ctx,std::uint32_t index,bool effects,WheelContact& contact) {
    const auto from=vertex(ctx,index);
    const auto down=var_vec_rotate({v(0),v(0),v(-100)},body_source(ctx.physics.body(ctx.ids.p_aracgovde).angles));
    auto local=from+down;
    auto world=from;
    world.z=world.z-v(100);
    const auto to=var_vec_scale((local+world),v(0.5));
    contact={ctx.physics.trace(native(from),native(to),contact_mode,ctx.ids.p_aracgovde),WheelEffect::none};
    if(!contact.hit.trace_hit) return v(-11111);
    if(effects) {
        const auto& s=ctx.state;
        if(s.arac_pati_aktif.raw==v(1).raw && index!=4 && index!=3)
            contact.effect=s.toprak_yol_ok.raw==0?WheelEffect::asphalt_smoke:WheelEffect::wheelspin_dust;
        else if(s.toprak_yol_ok.raw==v(1).raw && s.arac_ivme.raw!=0)
            contact.effect=WheelEffect::road_dust;
        if(contact.effect!=WheelEffect::none) {
            const char* helper=contact.effect==WheelEffect::asphalt_smoke?"asfalt_duman_cik":
                               contact.effect==WheelEffect::wheelspin_dust?"pati_toz_cik":"toprak_toz_cik";
            ctx.effect(helper,contact.hit.target,contact.hit.normal,ctx.ids.p_aracgovde);
        }
    }
    return property(contact.hit.target.z);
}

void SourceMotion::contact_angles(GameContext& ctx,Var time,bool robot,Var yaw,bool snap) {
    // Original one-based body vertices: front-left4/front-right3/rear-left2/rear-right1.
    const Var fl=contact_height(ctx,4,!robot,wheel_contacts_[0]);
    const Var fr=contact_height(ctx,3,!robot,wheel_contacts_[1]);
    const Var rl=contact_height(ctx,2,!robot,wheel_contacts_[2]);
    const Var rr=contact_height(ctx,1,!robot,wheel_contacts_[3]);
    const Var front=pair_height(fl,fr),rear=pair_height(rl,rr);
    const Var left=pair_height(fl,rl),right=pair_height(fr,rr);
    const Var missing=v(-11111);
    auto& s=ctx.state;
    auto& body=ctx.physics.body(ctx.ids.p_aracgovde);
    const Var rate=v(snap?4:(robot?2.5:2));
    Var tilt=v(0),roll=v(0);
    ScriptVec scratch=robot?ScriptVec{}:angle_scratch_;
    if(front.raw!=missing.raw && rear.raw!=missing.raw) {
        const auto back_left=vertex(ctx,2);
        scratch=vertex(ctx,1);
        auto back=var_vec_scale(back_left+scratch,v(0.5));
        const auto front_left=vertex(ctx,4);
        scratch=vertex(ctx,3);
        auto forward=var_vec_scale(front_left+scratch,v(0.5));
        back.z=rear;
        forward.z=front;
        scratch=var_vec_to_angle(forward-back,scratch);
        const Var gap=var_ang(scratch.y-property(body.angles.y));
        if(snap) {
            tilt=gap;
            s.c4_yere_carpma=v(100);
        } else {
            const Var gain=s.c4_yere_carpma/v(100);
            // sign is multiplied BEFORE time in the original slope expression.
            Var step=v(robot?5:4)*var_sign(gap)*time*gain;
            const Var proportional=v(robot?0.125:0.1)*gap*gain*var_sqrt(v(2)*time);
            if(var_abs(step).raw>var_abs(proportional).raw) step=proportional;
            if(var_sign(gap).raw!=var_sign(var_ang(gap-step)).raw) {
                step=gap;
                s.c4_yere_carpma=v(100);
            }
            if(step.raw==0) s.c4_yere_carpma=v(100);
            tilt=step;
        }
    } else {
        tilt=fallback(ctx,time,rate,false);
    }
    if(left.raw!=missing.raw && right.raw!=missing.raw) {
        const auto front_left=vertex(ctx,4);
        scratch=vertex(ctx,2);
        auto l=var_vec_scale(front_left+scratch,v(0.5));
        const auto front_right=vertex(ctx,3);
        scratch=vertex(ctx,1);
        auto r=var_vec_scale(front_right+scratch,v(0.5));
        l.z=left;
        r.z=right;
        scratch=var_vec_to_angle(l-r,scratch);
        const Var gap=var_ang(scratch.y-property(body.angles.z));
        const Var gain=s.c4_yere_carpma/v(100);
        if(snap) {
            roll=gap;
        } else {
            Var step=v(robot?5:4)*var_sign(gap)*time*gain;
            const Var proportional=v(robot?0.125:0.1)*gap*gain*var_sqrt(v(2)*time);
            if(var_abs(step).raw>var_abs(proportional).raw) step=proportional;
            if(var_sign(gap).raw!=var_sign(var_ang(gap-step)).raw) step=gap;
            roll=step;
        }
    } else {
        roll=fallback(ctx,time,rate,true);
    }
    if(!robot) angle_scratch_=scratch;
    ctx.physics.rotate(ctx.ids.p_aracgovde,{number(yaw),number(tilt),number(roll)},physics::Mode::ignore_passable);
}

void SourceMotion::ground_angles(GameContext& ctx,Var time) {
    wheel_contacts_={};
    if(ctx.state.c4_eski_havazaman.raw>=v(1).raw) return;
    if(landing_airtime_.raw>v(33).raw) {
        ctx.sound("arac_dusme_wav",var_min(landing_airtime_*v(1.4),v(100)));
        landing_airtime_=v(0);
        landing_animation_=true;
    }
    contact_angles(ctx,time,false,v(0),false);
}

void SourceMotion::robot_ground_angles(GameContext& ctx,Var time,Var yaw,bool snap) {
    contact_angles(ctx,time,true,yaw,snap);
}

bool SourceMotion::consume_landing_animation() {
    const bool requested=landing_animation_;
    landing_animation_=false;
    return requested;
}

void SourceMotion::shadows(GameContext& ctx) {
    auto& body=ctx.physics.body(ctx.ids.p_aracgovde);
    auto& shadow=ctx.physics.body(ctx.ids.p_arac_golge);
    const auto previous=body_source(shadow.angles);
    const auto from=body_source(body.position);
    auto to=from;
    to.z=to.z-v(1000);
    const auto hit=ctx.physics.trace(native(from),native(to),contact_mode,ctx.ids.p_aracgovde);
    ScriptVec pose;
    if(hit.trace_hit) {
        shadow.position=native(body_source(hit.target));
        const auto normal=var_vec_rotate(body_source(hit.normal),{v(0)-property(body.angles.x),v(0),v(0)});
        pose={property(body.angles.x),v(0)-var_asin(normal.x),v(0)-var_asin(normal.y)};
    } else {
        shadow.position=native(to);
        pose=body_source(body.angles);
    }
    if(var_abs(var_ang(pose.y)).raw>v(35).raw || var_abs(var_ang(pose.z)).raw>v(35).raw) pose=previous;
    shadow.angles=native(pose);
    ctx.copy_pose(ctx.ids.p_robot_golge,ctx.ids.p_arac_golge);
}
} // namespace c4::game
