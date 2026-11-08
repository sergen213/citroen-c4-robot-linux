#include "WorldPaths.hpp"
#include "WorldAmbience.hpp"
#include <algorithm>
#include <stdexcept>

namespace c4::game {
namespace {
using namespace physics;
struct TrioBinding {
    std::string_view path;
    std::array<std::string_view,3> fish;
    double travel,swim;
    bool gated,sword;
};
// Actual authored order of owner entities, not an invented trajectory.
constexpr TrioBinding trio_bindings[]{
    {"fish_path_sword2",{"fish_m_sword2a","fish_m_sword2b","fish_m_sword2c"},120,.65,true,true},
    {"fish_path_sword1",{"fish_m_sword1a","fish_m_sword1b","fish_m_sword1c"},150,.65,true,true},
    {"fish_path_shark1",{"fish_m_shark1a","fish_m_shark1b","fish_m_shark1c"},380,2,false,false},
    {"fish_path_shark2",{"fish_m_shark2a","fish_m_shark2b","fish_m_shark2c"},280,2,true,false}
};
constexpr std::string_view school_paths[]{"fish_path_hamsi1","fish_path_hamsi2"};
constexpr std::array<std::string_view,6> school_fish[]{
    {"hamsi_suru_1a","hamsi_suru_1b","hamsi_suru_1c","hamsi_suru_1d","hamsi_suru_1e","hamsi_suru_1f"},
    {"hamsi_suru_2a","hamsi_suru_2b","hamsi_suru_2c","hamsi_suru_2d","hamsi_suru_2e","hamsi_suru_2f"}
};
EntityId find(const assets::WorldScene& world,std::string_view action) {
    for(EntityId id=0;id<world.entities.size();++id)if(world.entities[id].action==action)return id;
    throw std::runtime_error("Missing original path actor: "+std::string(action));
}
void flags(assets::WorldEntity& entity,std::uint32_t set) {
    entity.source_flags_set|=set;entity.source_flags_clear&=~set;
}
void scale(GameContext& c,EntityId id,Var value) {
    const auto published=static_cast<float>(number(value));
    c.world.entities[id].scale={published,published,published};
    c.physics.body(id).scale=native(ScriptVec{value,value,value});
}
bool eligible(GameContext& c,EntityId savedmy,std::uint64_t last,FrameTiming timing) {
    if(timing.frame_index<=last||c.state.freeze_mode>=v(2))return false;
    return !(c.state.freeze_mode==v(1)&&savedmy!=no_entity);
}
void require_path(GameContext& c,EntityId path) {
    const auto& group=c.group(path);
    if(group.source_vertices.size()<4)throw std::runtime_error("Original actor path lacks source vertices1-4");
    // Validate the original authored clip; no guessed flight spline or synthesized mesh.
    (void)assets::animation_sample(group,"Frame",0,true);
}
ScriptVec vertex(GameContext& c,EntityId path,std::uint32_t index) {
    return body_source(c.physics.vec_for_vertex(path,index,c.visuals.at(path).sample));
}
void position_fish(GameContext& c,EntityId fish,EntityId path,Var travel,Var swim,ScriptVec& angles) {
    c.animate(path,"Frame",var_modulo(travel,v(100)),true);
    const auto anchor=vertex(c,path,4);
    angles=var_vec_to_angle(vertex(c,path,1)-anchor,angles);
    auto& body=c.physics.body(fish);auto pose=body_source(body.angles);
    body.position=native(anchor);pose.x=angles.x;pose.y=angles.y;body.angles=native(pose);
    c.animate(fish,"Frame",var_modulo(swim,v(100)),true);
}
Var smooth_angle(Var current,Var target,Var gain,Var time) {
    const auto previous=current;
    current+=gain*var_ang(target-current)*v(.5)*var_sqrt(v(2)*time);
    if(var_sign(var_ang(target-previous))!=var_sign(var_ang(target-current)))current=target;
    return current;
}
}
void prepare_world_paths(assets::WorldScene& world,SharedState& state,std::vector<VisualState>& visuals) {
    using namespace physics;
    for(EntityId id=0;id<world.entities.size();++id) {
        auto& entity=world.entities[id];auto& visual=visuals.at(id);
        if(entity.action=="GMA_heli_path") { flags(entity,0x300u);visual.invisible=true; }
        for(const auto& binding:trio_bindings) {
            if(entity.action==binding.path){flags(entity,0x300u);visual.invisible=true;}
            if(std::find(binding.fish.begin(),binding.fish.end(),entity.action)==binding.fish.end())continue;
            flags(entity,0x200u);
            if(binding.gated&&!state.particle_count) {
                flags(entity,0x100u);visual.invisible=true;entity.skills[18]=0;continue;
            }
        }
        for(std::size_t school=0;school<2;++school) {
            const bool path=entity.action==school_paths[school];
            const bool fish=std::find(school_fish[school].begin(),school_fish[school].end(),entity.action)!=school_fish[school].end();
            if(!path&&!fish)continue;
            flags(entity,path?0x300u:0x200u);if(path)visual.invisible=true;
            if(!state.particle_count){flags(entity,0x100u);visual.invisible=true;entity.skills[18]=0;continue;}
        }
    }
}
std::string_view world_paths_source_owner(std::string_view action) {
    if(action=="GMA_heli_govde"||action=="GMA_heli_pervane"||action=="GMA_heli_path")return "GMA_heli_govde";
    for(const auto& binding:trio_bindings)
        if(action==binding.path||std::find(binding.fish.begin(),binding.fish.end(),action)!=binding.fish.end())return binding.fish[1];
    for(std::size_t i=0;i<2;++i)
        if(action==school_paths[i]||std::find(school_fish[i].begin(),school_fish[i].end(),action)!=school_fish[i].end())return school_paths[i];
    return {};
}
WorldPaths::WorldPaths(GameContext& c) {
    using physics::v;
    heli_.body=find(c.world,"GMA_heli_govde");heli_.rotor=find(c.world,"GMA_heli_pervane");heli_.path=find(c.world,"GMA_heli_path");
    require_path(c,heli_.path);
    configured_.helicopters=heli_.body!=no_entity&&heli_.rotor!=no_entity&&heli_.path!=no_entity;
    for(std::size_t group=0;group<trios_.size();++group) {
        auto& trio=trios_[group];const auto& binding=trio_bindings[group];
        trio.enabled=!binding.gated||bool(c.state.particle_count);
        const auto count=std::count_if(c.world.entities.begin(),c.world.entities.end(),[&](const auto& entity){
            return std::find(binding.fish.begin(),binding.fish.end(),entity.action)!=binding.fish.end();
        });
        const bool path=std::any_of(c.world.entities.begin(),c.world.entities.end(),[&](const auto& entity){return entity.action==binding.path;});
        configured_.trios+=path&&count==static_cast<std::ptrdiff_t>(binding.fish.size());
        if(!trio.enabled)configured_.gated_entities+=static_cast<std::size_t>(count);
        // The All0 early return precedes pointer assignments, including the owner pointer.
        if(!trio.enabled)continue;
        trio.path=find(c.world,binding.path);require_path(c,trio.path);
        for(std::size_t i=0;i<3;++i)trio.fish[i]=find(c.world,binding.fish[i]);
        trio.travel_speed=v(binding.travel);trio.swim_speed=v(binding.swim);
    }
    for(std::size_t group=0;group<schools_.size();++group) {
        auto& school=schools_[group];school.enabled=bool(c.state.particle_count);
        const auto count=std::count_if(c.world.entities.begin(),c.world.entities.end(),[&](const auto& entity){
            return entity.action==school_paths[group]||std::find(school_fish[group].begin(),school_fish[group].end(),entity.action)!=school_fish[group].end();
        });
        configured_.schools+=count==static_cast<std::ptrdiff_t>(school.fish.size()+1);
        if(!school.enabled)configured_.gated_entities+=static_cast<std::size_t>(count);
        if(!school.enabled)continue;
        school.path=find(c.world,school_paths[group]);require_path(c,school.path);
        for(std::size_t i=0;i<6;++i)school.fish[i]=find(c.world,school_fish[group][i]);
    }
    entity_to_record_.assign(c.world.entities.size(),-1);
    for(EntityId id=0;id<c.world.entities.size();++id)if(!world_paths_source_owner(c.world.entities[id].action).empty()) {
        if(bound_entity_count_==bound_entities_.size())throw std::length_error("Original path action dispatch capacity exceeded");
        auto& binding=bindings_[bound_entity_count_];
        if(id==heli_.body)binding.kind=Kind::heli_body;
        if(id==heli_.rotor)binding.kind=Kind::heli_rotor;
        if(id==heli_.path)binding.kind=Kind::heli_path;
        for(std::size_t group=0;group<trios_.size();++group)if(trios_[group].enabled) {
            if(id==trios_[group].path)binding={Kind::trio_path,static_cast<std::uint8_t>(group),0};
            for(std::size_t fish=0;fish<3;++fish)if(id==trios_[group].fish[fish])binding={Kind::trio_fish,static_cast<std::uint8_t>(group),static_cast<std::uint8_t>(fish)};
        }
        for(std::size_t group=0;group<schools_.size();++group)if(schools_[group].enabled) {
            if(id==schools_[group].path)binding={Kind::school_path,static_cast<std::uint8_t>(group),0};
            for(std::size_t fish=0;fish<6;++fish)if(id==schools_[group].fish[fish])binding={Kind::school_fish,static_cast<std::uint8_t>(group),static_cast<std::uint8_t>(fish)};
        }
        entity_to_record_[id]=static_cast<std::int16_t>(bound_entity_count_);bound_entities_[bound_entity_count_++]=id;
    }
}
void WorldPaths::start_trio(GameContext& c,Trio& trio) {
    if(!trio.path_registered||!std::all_of(trio.registered.begin(),trio.registered.end(),[](bool registered){return registered;}))return;
    trio.phase={c.random(physics::v(16)),physics::v(33)+c.random(physics::v(17)),physics::v(66)+c.random(physics::v(16))};
    trio.started=true;
}
void WorldPaths::start_school(GameContext& c,School& school) {
    if(!std::all_of(school.registered.begin(),school.registered.end(),[](bool registered){return registered;}))return;
    constexpr int offsets[]{0,16,33,50,66,83};
    for(std::size_t i=0;i<6;++i)school.locals[i].phase=physics::v(offsets[i])+c.random(physics::v(i%2?9:8));
    school.started=true;
}
void WorldPaths::initialize(GameContext&,FrameTiming timing) {
    if(live_)return;
    live_=true;
    origin_frame_=timing.frame_index;
    standalone_queued_.fill(true);
    heli_.last_frame=heli_.rotor_wait_frame=timing.frame_index;
    heli_.cycle={};heli_.flicker={};heli_.travel={};heli_.angles={};heli_.started=false;heli_.flip=false;heli_.rotor_registered=false;
    heli_.body_entered=false;heli_.rotor_entered=false;heli_.path_registered=false;
    for(auto& trio:trios_) {
        trio.last_frame=timing.frame_index;trio.registered.fill(false);trio.phase={};trio.travel={};trio.swim={};trio.angles={};trio.started=false;trio.path_registered=false;
    }
    for(auto& school:schools_) {
        school.last_frame=timing.frame_index;school.registered.fill(false);school.locals={};school.angles={};school.started=false;
    }
}
void WorldPaths::update_trio(GameContext& c,Trio& trio,Var time) {
    using namespace physics;
    if(c.visuals.at(trio.fish[0]).invisible)return;
    for(std::size_t i=0;i<3;++i)position_fish(c,trio.fish[i],trio.path,trio.travel+trio.phase[i],trio.swim+trio.phase[i],trio.angles);
    trio.swim=var_modulo(trio.swim+v(6.25)/trio.swim_speed*time,v(100));
    trio.travel=var_modulo(trio.travel+v(6.25)/trio.travel_speed*time,v(100));
}
void WorldPaths::update_school(GameContext& c,School& school,Var time) {
    using namespace physics;
    if(c.visuals.at(school.fish[0]).invisible)return;
    for(std::size_t i=0;i<6;++i) {
        const auto& local=school.locals[i];
        position_fish(c,school.fish[i],school.path,local.travel+local.phase,local.swim+local.phase,school.angles);
    }
    // All six poses precede all six numeric updates, preserving source RNG call order.
    for(std::size_t i=0;i<6;++i) {
        auto& local=school.locals[i];
        local.swim=var_modulo(local.swim+v(6.25)/v(2)*time*local.speed/v(100),v(100));
        local.travel=var_modulo(local.travel+(v(6.25)/v(125))*time*local.speed/v(100),v(100));
        if(local.timer>=v(100)) {
            if(local.timer==v(100)){local.target=v(100)-c.random(v(70));local.initial=local.speed;}
            local.speed=local.initial+(local.target-local.initial)*(local.timer-v(100))/v(100);
            local.timer+=v(6.25)/v(.5)*time;
            if(local.timer>v(200))local.timer={};
        } else {
            local.timer+=v(6.25)/(v(i<3?2:1)+(c.random(v(200))/v(100)))*time;
            local.timer=var_min(local.timer,v(100));
        }
    }
}
void WorldPaths::update_helicopter(GameContext& c,Var time) {
    using namespace physics;
    c.animate(heli_.path,"Frame",heli_.travel,true);
    auto anchor=vertex(c,heli_.path,4);
    auto& desired=heli_.angles;desired=var_vec_to_angle(vertex(c,heli_.path,1)-anchor,desired);
    auto& body=c.physics.body(heli_.body);
    if(!heli_.started){body.angles=native(desired);body.position=native(anchor);heli_.started=true;}
    auto pose=body_source(body.angles);
    pose.x=smooth_angle(pose.x,desired.x,v(.2),time);
    pose.y=smooth_angle(pose.y,desired.y,v(.1),time);
    anchor=vertex(c,heli_.path,3);desired=var_vec_to_angle(vertex(c,heli_.path,2)-anchor,desired);
    pose.z=smooth_angle(pose.z,desired.y,v(.1),time);
    body.position=native(anchor);body.angles=native(pose);c.copy_pose(heli_.rotor,heli_.body);
    tune_world_helicopter(c,heli_.body);
    c.animate(heli_.body,"frame",heli_.cycle,true);c.animate(heli_.rotor,"frame",heli_.cycle,true);
    heli_.cycle=var_modulo(heli_.cycle+v(6.25)/v(1.5)*time,v(100));
    heli_.flicker+=v(6.25)/v(.02)*time;
    if(heli_.flicker>=v(100)) {
        auto& rotor=c.visuals.at(heli_.rotor);rotor.alpha=v(heli_.flip?50:30);rotor.transparent=true;heli_.flip=!heli_.flip;
    }
    heli_.flicker=var_modulo(heli_.flicker,v(100));
    heli_.travel=c.state.ghost_yaris_start==v(1)?var_modulo(heli_.travel+v(6.25)/v(150)*time,v(100)):Var{};
}
std::span<const EntityId> WorldPaths::bound_entities() const { return {bound_entities_.data(),bound_entity_count_}; }
bool WorldPaths::dispatch_entity(GameContext& c,FrameTiming timing,EntityId id) {
    using physics::v;
    if(!live_||id>=entity_to_record_.size()||entity_to_record_[id]<0)return false;
    if(!eligible(c,id,origin_frame_,timing))return true;
    const auto& binding=bindings_[static_cast<std::size_t>(entity_to_record_[id])];
    switch(binding.kind) {
    case Kind::finite_gate:return false;
    case Kind::heli_path:heli_.path_registered=true;return false;
    case Kind::heli_body:
        if(!eligible(c,id,heli_.last_frame,timing))return true;
        heli_.last_frame=timing.frame_index;
        if(!heli_.body_entered){heli_.body_entered=true;return true;} // Source morph; wait(1).
        if(heli_.rotor_registered&&heli_.path_registered)update_helicopter(c,timing.originaltime_step_ticks);
        return true;
    case Kind::heli_rotor:
        if(heli_.rotor_registered)return false;
        if(!eligible(c,id,heli_.rotor_wait_frame,timing))return true;
        heli_.rotor_wait_frame=timing.frame_index;
        if(!heli_.rotor_entered){heli_.rotor_entered=true;return true;} // Source morph; wait(1).
        heli_.rotor_registered=true;return false;
    case Kind::trio_path:trios_[binding.group].path_registered=true;return false;
    case Kind::trio_fish: {
        auto& trio=trios_[binding.group];
        if(!trio.registered[binding.fish]) {
            // These source bodies are queued, not constructor/prePhysics work.
            c.physics.body(id).angles={};c.world.entities[id].angles={};
            const bool sword=trio_bindings[binding.group].sword;
            scale(c,id,v(7)*(v(sword?50:100)+c.random(v(sword?50:150)))/v(100));
            trio.registered[binding.fish]=true;
        }
        if(binding.fish!=1)return false;
        if(!eligible(c,id,trio.last_frame,timing))return true;
        trio.last_frame=timing.frame_index;
        if(!trio.started)start_trio(c,trio);
        if(trio.started)update_trio(c,trio,timing.originaltime_step_ticks);
        return true;
    }
    case Kind::school_path: {
        auto& school=schools_[binding.group];
        if(!eligible(c,id,school.last_frame,timing))return true;
        school.last_frame=timing.frame_index;
        if(!school.started)start_school(c,school);
        if(school.started)update_school(c,school,timing.originaltime_step_ticks);
        return true;
    }
    case Kind::school_fish: {
        auto& registered=schools_[binding.group].registered[binding.fish];
        if(!registered) {
            scale(c,id,(v(100)+c.random(v(60)))/v(100));
            c.physics.body(id).angles={};c.world.entities[id].angles={};registered=true;
        }
        return false;
    }
    }
    return false;
}
void WorldPaths::update(GameContext& c,FrameTiming timing) {
    // Production Core supplies shared source/destination FIFO dispatch; standalone
    // path checks retain authored placement order without claiming inter-family parity.
    for(std::size_t i=0;i<bound_entity_count_;++i)if(standalone_queued_[i])
        standalone_queued_[i]=dispatch_entity(c,timing,bound_entities_[i]);
}
void WorldPaths::teardown() { live_=false; }
WorldPaths::Coverage WorldPaths::coverage() const {
    auto result=configured_;
    if(!live_)return result;
    result.active_helicopters=heli_.started;
    for(const auto& trio:trios_)result.active_trios+=trio.enabled&&trio.started;
    for(const auto& school:schools_)result.active_schools+=school.enabled&&school.started;
    return result;
}
} // namespace c4::game
