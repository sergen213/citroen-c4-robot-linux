#include "ParticleCallbacks.hpp"
#include "game/GameState.hpp"
#include "physics/NativeNumeric.hpp"

#include <stdexcept>

namespace c4::game {
namespace {
using physics::Var;
using physics::v;
Var random(GameContext& context, double maximum) {
    return context.random(v(maximum));
}

std::array<Var,3> anchor(GameContext& context, bool camera) {
    const auto position=physics::body_source(context.physics.body(camera ? context.ids.p_camera_target :
                                                                         context.ids.p_aracgovde).position);
    return {position.x,position.y,position.z};
}

void follow_anchor(GameContext& context, SourceParticle& p, bool camera, Var factor) {
    const auto current=anchor(context,camera);
    for(std::size_t i=0;i<3;++i)
        p.position[i]=p.position[i]+factor*(current[i]-p.previous_anchor[i])*p.alpha/p.initial_alpha;
    p.previous_anchor=current;
}

void nitro_shape(GameContext& context, SourceParticle& p, const NitroParticleContext& nitro) {
    const auto beam=p.source=="nitrospezial1" ? 0 : 1;
    const auto ordinal=v(p.ordinal);
    for(std::size_t i=0;i<3;++i)
        p.position[i]=nitro.start[beam][i]+(nitro.end[beam][i]-nitro.start[beam][i])*(ordinal/v(26));
    p.alpha=p.rise*((v(26)-ordinal)/v(26))*((v(100)-random(context,30))/v(100));
    p.size=v(15)-(v(4)*(p.rise-p.alpha)/p.rise);
}

void smoke_property(GameContext& context, SourceParticle& p, Var ticks) {
    const bool robot=p.source=="robotoz_effect";
    follow_anchor(context,p,robot || p.source=="toz_effect",v(robot ? .75 : 1));
    p.velocity[2]=p.rise+(p.acceleration*(p.initial_alpha-p.alpha)/p.initial_alpha);
    p.size=p.size+v(robot ? 1 : p.source=="toz_effect" ? .5 : 2.5)*ticks;
    p.alpha=p.alpha-v(robot ? 1.125 : p.source=="babaduman_effect" ? .6 : .9)*ticks;
    if(p.alpha.raw<=0)p.lifespan=v(0);
}
} // namespace

void initialize_particle(GameContext& context, SourceParticle& p, std::string_view callback,
                         const NitroParticleContext& nitro) {
    p.source=callback;
    if(callback=="nitrospezial1" || callback=="nitrospezial2") {
        p.texture=3;
        p.velocity={};
        p.size=v(12);
        p.alpha=v(40);
        p.rise=p.alpha; // Source skill_b, not the dust skill_a initial-alpha field.
        p.gravity=v(0);
        p.additive=p.bright=true;
        p.transparent=true;
        p.move=false;
        nitro_shape(context,p,nitro);
        return;
    }
    if(callback=="karasmoke_effect") {
        p.velocity={v(0),v(0),random(context,3)};
        p.move=true;
        p.size=v(12);
        p.texture=4;
        p.alpha=v(10);
        return;
    }
    if(callback=="kivilcim_effect" || callback=="kivilkay_effect") {
        const bool follow=callback=="kivilkay_effect";
        p.texture=2;
        if(follow)p.previous_anchor=anchor(context,true);
        else p.previous_anchor[0]=v(0);
        p.size=v(follow ? 5 : 4);
        p.move=true;
        p.alpha=v(follow ? 25 : 35)+random(context,10);
        if(follow)p.initial_alpha=p.alpha;
        p.transparent=true;
        p.gravity=v(.8);
        p.bright=p.additive=true;
        return;
    }
    const bool dust=callback=="toz_effect";
    const bool robot=callback=="robotoz_effect";
    const bool smoke=callback=="babaduman_effect";
    const bool ground=callback=="babatoz_effect";
    if(!dust && !robot && !smoke && !ground && callback!="patitoz_effect")
        throw std::invalid_argument("Unknown original particle callback");
    p.velocity[0]=random(context,robot ? 20 : smoke ? 4 : 8)-v(robot ? 10 : smoke ? 2 : 4);
    p.velocity[1]=random(context,robot ? 20 : smoke ? 4 : 8)-v(robot ? 10 : smoke ? 2 : 4);
    p.velocity[2]=v(dust || robot ? 1 : 0)+random(context,robot ? 6 : dust ? 2 : 1);
    p.rise=v(robot ? 2 : 0)+random(context,robot ? 4 : smoke ? 1 : 2);
    p.acceleration=v(robot ? 2 : dust || smoke ? 1 : 3)+random(context,robot || smoke ? 2 : dust ? 1 : 4);
    p.previous_anchor=anchor(context,dust || robot);
    p.move=true;
    p.size=v(robot ? 20 : dust ? 32 : 4)+random(context,robot ? 20 : dust ? 32 : 4);
    p.texture=smoke ? 1 : 0;
    const auto opacity=v(1.5)+(random(context,100)/v(200));
    if(dust)p.alpha=v(1.5)*opacity*v(16);
    else if(robot)p.alpha=opacity*v(24);
    else if(ground)p.alpha=v(1.2)*opacity*physics::var_min(physics::var_abs(context.state.arac_ivme/v(6)),v(16));
    else p.alpha=v(smoke ? .6 : 1.2)*opacity*physics::var_max(v(16)-physics::var_abs(context.state.arac_ivme/v(16)),v(1));
    p.initial_alpha=p.alpha;
}

bool step_particle_engine(SourceParticle& p, Var ticks) {
    //100439dc: test AFTER callback but BEFORE subtracting the current time_step.
    if(p.lifespan.raw<=0)return false;
    p.lifespan=p.lifespan-ticks;
    if(p.move) {
        for(std::size_t i=0;i<3;++i)p.position[i]=p.position[i]+p.velocity[i].scaled(ticks);
        if(p.gravity.raw!=0)p.velocity[2]=p.velocity[2]-p.gravity.scaled(ticks);
    }
    return true;
}

bool step_particle(GameContext& context, SourceParticle& p, Var ticks,
                   const NitroParticleContext& nitro) {
    if(p.source=="nitrospezial1" || p.source=="nitrospezial2") {
        nitro_shape(context,p,nitro);
        p.lifespan=v(nitro.active ? 1 : 0)*(v(1)-v(nitro.freeze));
    } else if(p.source=="kivilcim_effect" || p.source=="kivilkay_effect") {
        const bool follow=p.source=="kivilkay_effect";
        if(follow)follow_anchor(context,p,true,v(.75));
        p.alpha=p.alpha-v(follow ? 4 : 2)*ticks;
        if(p.alpha.raw<0)p.lifespan=v(0);
    } else if(p.source=="karasmoke_effect") {
        p.alpha=p.alpha-v(.6)*ticks;
        if(p.alpha.raw<=0)p.lifespan=v(0);
    } else if(p.source=="toz_effect" || p.source=="babatoz_effect" ||
              p.source=="robotoz_effect" || p.source=="babaduman_effect" ||
              p.source=="patitoz_effect") {
        smoke_property(context,p,ticks);
    } else throw std::invalid_argument("Unknown original particle callback");
    return step_particle_engine(p,ticks);
}
} // namespace c4::game
