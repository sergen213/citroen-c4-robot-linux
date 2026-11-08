#include "SourceEffects.hpp"
#include "ParticleCallbacks.hpp"
#include "game/GameState.hpp"
#include "game/CameraProjection.hpp"
#include "assets/World.hpp"
#include "physics/PhysicsWorld.hpp"
#include "physics/NativeNumeric.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace c4::game {
namespace {
using physics::Var;
using QVec=std::array<Var,3>;
constexpr std::uint32_t capacity=10000;
constexpr std::uint32_t none=std::numeric_limits<std::uint32_t>::max();
QVec quantized(physics::Vec3 value) {
    auto source=physics::body_source(value);
    return {source.x,source.y,source.z};
}
Var clamp01(Var a) { return physics::var_clamp(a,physics::v(0),physics::v(1)); }
QVec plus(QVec a,QVec b) { for(unsigned i=0;i<3;++i)a[i]=a[i]+b[i];return a; }
constexpr std::array<std::string_view,5> texture_names{"toz3.tga","duman3.tga","kivilcim_par.tga","laser_red.tga","black_smoke.tga"};
}

struct SourceEffects::Impl {
    explicit Impl(std::filesystem::path root):root(std::move(root)) {
        particles.resize(capacity);
        next.resize(capacity);
        vertices.resize(capacity*4);
        indices.resize(capacity*6);
        batches.resize(capacity);
        reset_pool();
    }
    std::filesystem::path root;
    std::array<assets::Image,5> textures;
    std::array<std::array<std::array<float,2>,4>,5> texture_uv{};
    std::vector<SourceParticle> particles;
    std::vector<std::uint32_t> next;
    std::vector<EffectVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<EffectBatch> batches;
    std::vector<EntityId> ambient;
    NitroParticleContext nitro;
    std::array<std::uint32_t,2> nitro_ordinals{};
    EffectFrame frame;
    EffectCounts live_counts;
    std::uint32_t free_head{},active_head{none},active_count{};
    std::uint64_t revision{};
    bool initialized{},nitro_started{},nitro_enabled{};
    void reset_pool() {
        for(std::uint32_t i=0;i<capacity;++i)next[i]=i+1;
        next.back()=none;free_head=0;active_head=none;active_count=0;
        // UI2 may clear simulation after UI1 publishes the frame selected for draw.
        live_counts={};
        nitro={};nitro_ordinals={};nitro_started=false;
    }
    void effect(GameContext&,std::string_view callback,Var count,QVec position,QVec velocity={}) {
        // Original effect() clamps each integer-count call, not the complete emitter.
        const auto integer=std::min<std::uint32_t>(static_cast<std::uint32_t>(std::abs(count.raw/1024)),capacity-10);
        auto recycle=active_head;
        for(std::uint32_t i=0;i<integer;++i) {
            std::uint32_t slot;
            if(free_head!=none) {
                slot=free_head;free_head=next[slot];next[slot]=active_head;active_head=slot;++active_count;
            } else {
                if(recycle==none) { ++live_counts.allocation_failures;break; }
                slot=recycle;recycle=next[slot];++live_counts.recycled;
            }
            auto& p=particles[slot];p={};p.position=position;p.velocity=velocity;
            // Original effect ABI roundtrips only the position through float32.
            for(auto& component:p.position)component=Var::from_float(component.to_float());
            p.size=physics::v(4);p.alpha=physics::v(50);p.lifespan=physics::v(80);p.source=callback;
            p.pending_initialization=true;
            ++live_counts.emitted;
        }
    }
    void emit(GameContext& ctx,const EffectRequest& r) {
        auto random=[&](double max){return ctx.random(physics::v(max));};
        const auto count=ctx.state.particle_count;
        if(r.helper!="kara_smoke"&&count.raw==0)return;
        auto pos=quantized(r.position),vel=pos;
        if(r.helper=="robotkum_toz_cik") {
            auto dist=physics::v(5)+random(10)/physics::v(2);vel[2]=vel[2]+dist;
            pos[0]=pos[0]+random(10)-physics::v(5);pos[1]=pos[1]+random(10)-physics::v(5);pos[2]=pos[2]+random(10);
            effect(ctx,"robotoz_effect",physics::v(4)*count,pos,vel);return;
        }
        if(r.helper=="toprak_toz_cik"||r.helper=="pati_toz_cik"||r.helper=="asfalt_duman_cik") {
            auto dist=physics::v(5)+random(10)/physics::v(2);
            auto tilt=physics::var_vec_to_angle(physics::body_source(r.normal)).y;
            auto pan=physics::property(ctx.physics.body(ctx.ids.p_aracgovde).angles.x);
            vel[0]=vel[0]+dist*physics::var_cos(pan)*physics::var_cos(tilt);
            vel[1]=vel[1]+dist*physics::var_sin(pan)*physics::var_cos(tilt);
            vel[2]=vel[2]+dist*physics::var_sin(tilt);
            bool smoke=r.helper=="asfalt_duman_cik";
            pos[0]=pos[0]+random(smoke?10:20)-physics::v(smoke?5:10);
            pos[1]=pos[1]+random(smoke?10:20)-physics::v(smoke?5:10);
            pos[2]=pos[2]+random(smoke?20:10);
            auto n=smoke?(physics::var_abs(ctx.state.arac_ivme).raw<physics::v(25).raw?physics::v(1):physics::v(2)):physics::v(2)*count;
            effect(ctx,smoke?"babaduman_effect":r.helper=="pati_toz_cik"?"patitoz_effect":"babatoz_effect",n,pos,vel);return;
        }
        if(r.helper=="kivilci_cik_fx"||r.helper=="kivilci_kay_fx") {
            bool sliding=r.helper=="kivilci_kay_fx";
            auto speed=ctx.state.arac_ivme;
            auto n=physics::v(4)*count*clamp01(((sliding?speed:physics::var_abs(speed))-physics::v(20))/physics::v(40));
            for(Var i=physics::v(0);i.raw<n.raw;i=i+physics::v(1)) {
                auto magnitude=physics::v(1)+random(1);if(sliding)magnitude=magnitude*physics::v(1.4);
                auto pan=random(300)+(sliding?physics::property(ctx.physics.body(ctx.ids.p_camera_target).angles.x):physics::v(0))+physics::v(30);
                auto tilt=physics::v(sliding?30:20)+random(50);
                QVec velocity{magnitude*physics::var_cos(pan)*magnitude*physics::var_cos(tilt),magnitude*physics::var_sin(pan)*magnitude*physics::var_cos(tilt),physics::v(2)*magnitude*physics::var_cos(tilt)};
                auto target=quantized(r.position);
                for(unsigned j=0;j<3;++j)target[j]=target[j]+(sliding?velocity[j]*physics::v(6)/physics::v(1.4):velocity[j]*physics::v(6));
                effect(ctx,sliding?"kivilkay_effect":"kivilcim_effect",physics::v(1),target,velocity);
            }return;
        }
        if(r.helper=="daire_toz_cik") {
            auto body=quantized(ctx.physics.body(ctx.ids.p_aracgovde).position);
            auto camera=quantized(ctx.physics.body(ctx.ids.p_camera_target).position);
            auto pan=physics::property(ctx.physics.body(ctx.ids.p_camera_target).angles.x),m=r.argument;
            for(int i=0;i<12;++i) {
                auto velocity=camera,position=body;
                const auto vx1=random(40),vx2=random(40);
                velocity[0]=velocity[0]+(vx1-physics::v(20))*m+(vx2-physics::v(20))*physics::var_cos(pan);
                const auto vy1=random(40),vy2=random(40);
                velocity[1]=velocity[1]+(vy1-physics::v(20))*m+(vy2-physics::v(20))*physics::var_sin(pan);
                velocity[2]=velocity[2]+random(10)-physics::v(10);
                const auto px1=random(100),px2=random(40);
                position[0]=position[0]+(px1-physics::v(50))*m+(px2-physics::v(20))*physics::var_cos(pan);
                const auto py1=random(100),py2=random(40);
                position[1]=position[1]+(py1-physics::v(50))*m+(py2-physics::v(20))*physics::var_sin(pan);
                position[2]=position[2]+random(10)-physics::v(10);
                effect(ctx,"toz_effect",count,position,velocity);
            }return;
        }
        if(r.helper=="kara_smoke") {
            pos[0]=pos[0]+random(20)-physics::v(10);pos[1]=pos[1]+random(20)-physics::v(10);
            effect(ctx,"karasmoke_effect",physics::v(2),pos,pos);return;
        }
        throw std::invalid_argument("Unmapped original particle emitter: "+std::string(r.helper));
    }
    void nitro_frame(GameContext& ctx) {
        if(!nitro_enabled)return;
        nitro.freeze=static_cast<int>(ctx.state.freeze_mode.to_float());
        nitro.active=ctx.state.arac_turbo.raw==physics::v(5).raw;
        if(nitro.active) {
            auto speed=physics::var_min(physics::var_abs(ctx.state.arac_ivme),physics::v(350));
            auto length=-physics::v(20)-physics::v(140)*speed/physics::v(350);
            auto& body=ctx.physics.body(ctx.ids.p_aracgovde);
            auto rotated=physics::var_vec_rotate({length,{},{}},physics::body_source(body.angles));
            QVec direction{rotated.x,rotated.y,rotated.z};
            const auto entity=ctx.ids.p_arac_kaporta;
            const auto sample=ctx.visuals.at(entity).sample;
            constexpr std::array<std::array<std::uint32_t,2>,2> vertices{{{1659,1665},{2255,2263}}};
            for(unsigned j=0;j<2;++j) {
                auto a=quantized(ctx.physics.vec_for_vertex(entity,vertices[j][0],sample));
                auto b=quantized(ctx.physics.vec_for_vertex(entity,vertices[j][1],sample));
                for(unsigned k=0;k<3;++k)nitro.start[j][k]=(a[k]+b[k]).scaled(physics::v(0.5));
                nitro.end[j]=plus(nitro.start[j],direction);
            }
            if(!nitro_started) {
                nitro_ordinals={};
                for(std::uint32_t i=1;i<26;++i)for(unsigned j=0;j<2;++j)
                    effect(ctx,j?"nitrospezial2":"nitrospezial1",physics::v(1),nitro.start[j]);
            }
        }
        nitro_started=nitro.active;
    }
    void snapshot(GameContext& ctx) {
        const bool no_particles=capture_source_view(ctx.state).no_particles;
        auto axes=physics::rotation_axes(physics::native(ctx.state.camera_angles));
        auto right=axes[1]*-1,up=axes[2];
        // Both pixel edges use original focalX. Native camera stores E=focalY/focalX.
        const auto aspect=ctx.state.camera_pixel_aspect;
        if(active_count&&!no_particles&&!(aspect>0))
            throw std::logic_error("Original particle camera effective aspect not configured");
        std::uint32_t vn=0,in=0,bn=0;
        for(auto slot=active_head;slot!=none;slot=next[slot]) {
            const auto& p=particles[slot];
            if(no_particles||p.pending_initialization||p.size.raw==0)continue;
            const float half=p.size.to_float()*0.5f;
            const physics::Vec3 center{p.position[0].to_float(),p.position[1].to_float(),p.position[2].to_float()};
            constexpr std::array<std::array<float,2>,4> corners{{{-1,1},{1,1},{1,-1},{-1,-1}}};
            const auto& uv=texture_uv[p.texture];
            const auto byte_alpha=std::clamp((static_cast<std::int64_t>(p.alpha.raw)*10)>>11,std::int64_t{0},std::int64_t{255});
            const auto alpha=static_cast<float>(byte_alpha)/255;
            constexpr float modulate2x_color=256.0f/255;
            const auto first=vn;
            for(unsigned j=0;j<4;++j) {
                auto position=center+right*(half*corners[j][0])+up*(half*corners[j][1]/aspect);
                vertices[vn++]={{{static_cast<float>(position.x),static_cast<float>(position.y),static_cast<float>(position.z)}},uv[j],{{modulate2x_color,modulate2x_color,modulate2x_color,alpha}}};
            }
            const auto first_index=in;
            for(auto j:std::array<std::uint32_t,6>{0,1,2,0,2,3})indices[in++]=first+j;
            batches[bn++]={first_index,6,p.texture,p.additive,p.bright,false,true,false};
        }
        frame.vertices={vertices.data(),vn};frame.indices={indices.data(),in};frame.batches={batches.data(),bn};
        frame.counts=live_counts;frame.counts.active=active_count;frame.revision=++revision;
        frame.source_anisotropy_raw=ctx.state.d3d_anisotropy.raw;
    }
};
SourceEffects::SourceEffects(const std::filesystem::path& root):impl_(std::make_unique<Impl>(root)){}
SourceEffects::~SourceEffects()=default;
void SourceEffects::initialize(GameContext& ctx) {
    if(impl_->initialized)return;
    auto paths=assets::resolve_resources(impl_->root/"bogazici.wmb",texture_names);
    for(unsigned i=0;i<texture_names.size();++i) {
        auto& image=impl_->textures[i];
        image=assets::load_image(paths.at(i));
        const auto width=static_cast<float>(image.width),height=static_cast<float>(image.height);
        const auto u0=.5f/width,v0=.5f/height;
        const auto u1=(width-.5f)/width,v1=(height-.5f)/height;
        // Original black_smoke is copied into a padded 32x32 texture without scaling.
        // Unpadded native storage preserves the same texel centers; all five have one mip level.
        impl_->texture_uv[i]={{{u0,v0},{u1,v0},{u1,v1},{u0,v1}}};
    }
    impl_->frame.textures=impl_->textures;impl_->frame.texture_sources=texture_names;
    for(EntityId i=0;i<ctx.world.entities.size();++i)if(ctx.world.entities[i].action=="kara_smoke") {
        ctx.visuals.at(i).invisible=true;
        ctx.physics.body(i).flags.passable=true;
        impl_->ambient.push_back(i);
    }
    impl_->nitro_enabled=ctx.state.particle_count.raw!=0;impl_->initialized=true;
}
void SourceEffects::emit(GameContext& ctx,const EffectRequest& request) {
    if(!impl_->initialized)throw std::logic_error("Original particle resources not initialized");
    if(ctx.state.freeze_mode>physics::v(0))return;
    impl_->emit(ctx,request);
}
void SourceEffects::run_processes(GameContext& ctx,FrameTiming) {
    if(!impl_->initialized)throw std::logic_error("Original particle resources not initialized");
    if(ctx.state.freeze_mode>physics::v(0))return;
    for(auto entity:impl_->ambient)impl_->emit(ctx,{"kara_smoke",ctx.physics.body(entity).position,{},entity,{}});
    impl_->nitro_frame(ctx);
}
void SourceEffects::update(GameContext& ctx,FrameTiming timing) {
    if(!impl_->initialized)throw std::logic_error("Original particle resources not initialized");
    if(ctx.state.freeze_mode>physics::v(0))return;
    auto slot=impl_->active_head,previous=none;
    while(slot!=none) {
        auto following=impl_->next[slot];
        auto& particle=impl_->particles[slot];
        bool alive;
        if(particle.pending_initialization) {
            particle.pending_initialization=false;
            if(particle.source=="nitrospezial1")particle.ordinal=++impl_->nitro_ordinals[0];
            if(particle.source=="nitrospezial2")particle.ordinal=++impl_->nitro_ordinals[1];
            initialize_particle(ctx,particle,particle.source,impl_->nitro);
            // Every original particle bitmap is 32-bit TGA; original loader sets BMAP alpha bit 0x100.
            particle.transparent=true;
            alive=step_particle_engine(particle,timing.originaltime_step_ticks);
        }else alive=step_particle(ctx,particle,timing.originaltime_step_ticks,impl_->nitro);
        if(!alive) {
            if(previous==none)impl_->active_head=following;else impl_->next[previous]=following;
            impl_->next[slot]=impl_->free_head;impl_->free_head=slot;--impl_->active_count;++impl_->live_counts.expired;
        }else previous=slot;
        slot=following;
    }
}
void SourceEffects::publish_frame(GameContext& ctx) {
    if(!impl_->initialized)throw std::logic_error("Original particle resources not initialized");
    // Original draw selects camera/view flags after the first UI callback pass.
    // Publication never invokes particle callbacks, advances life, or consumes RNG.
    impl_->snapshot(ctx);
}
void SourceEffects::clear(){impl_->reset_pool();}
const EffectFrame& SourceEffects::frame()const{return impl_->frame;}
std::string SourceEffects::diagnostics()const {
    std::ostringstream out;out<<"effects active="<<impl_->active_count<<" emitted="<<impl_->live_counts.emitted<<" expired="<<impl_->live_counts.expired<<" recycled="<<impl_->live_counts.recycled<<" allocation_failures="<<impl_->live_counts.allocation_failures;
    constexpr std::array<std::string_view,10> names{"karasmoke_effect","toz_effect","babatoz_effect","robotoz_effect","kivilcim_effect","kivilkay_effect","nitrospezial1","nitrospezial2","babaduman_effect","patitoz_effect"};
    for(auto name:names){std::uint32_t count=0;for(auto slot=impl_->active_head;slot!=none;slot=impl_->next[slot])if(impl_->particles[slot].source==name)++count;if(count)out<<' '<<name<<'='<<count;}
    return out.str();
}
} // namespace c4::game
