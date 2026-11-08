#include "game/WorldPaths.hpp"
#include "assets/Animation.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>

namespace {
using namespace c4::game;
using namespace c4::physics;
EntityId entity(const c4::assets::WorldScene& world, std::string_view action) {
    for(EntityId id=0;id<world.entities.size();++id)if(world.entities[id].action==action)return id;
    assert(false);return c4::game::no_entity;
}
void sample_equal(c4::assets::AnimationSample a,c4::assets::AnimationSample b) {
    assert(a.first_frame==b.first_frame&&a.second_frame==b.second_frame&&a.blend==b.blend);
}
void paths(const std::filesystem::path& resource,bool enabled) {
    auto world=c4::assets::load_world(resource,enabled?c4::assets::high_source_profile:c4::assets::original_source_profile);
    SharedState state;state.particle_count=v(enabled?2:0);
    std::vector<VisualState> visuals(world.entities.size());
    const auto before=world.entities;
    prepare_world_paths(world,state,visuals);
    unsigned gated=0;
    for(EntityId id=0;id<world.entities.size();++id) {
        const auto& e=world.entities[id];
        assert(e.angles==before[id].angles&&e.scale==before[id].scale);
        if(e.action.starts_with("fish_m_sword")||e.action.starts_with("fish_m_shark2")||
           e.action.starts_with("hamsi_suru_")||e.action.starts_with("fish_path_hamsi")) {
            ++gated;
            if(!enabled) {
                assert(visuals[id].invisible&&e.skills[18]==0);
                assert(e.angles==before[id].angles&&e.scale==before[id].scale);
                assert((e.source_flags_set&0x300u)==0x300u);
                if(e.action.starts_with("fish_m_sword")||e.action.starts_with("fish_m_shark2")) {
                    const auto& binding=world.models.at(e.model_index);
                    assert(binding.texture_resource.empty()&&binding.model);
                    assert(binding.skin_resources==c4::assets::resolve_model_skins(binding.resource,*binding.model,world.source_profile));
                }
            }
        }
    }
    assert(gated==23);
    assert(state.rng_calls==0);
    c4::physics::PhysicsWorld physics(world);
    c4::audio::Audio audio(48000,c4::audio::Mode::Offline);
    ComponentIds ids;SoundCache sounds;const std::filesystem::path root=resource.parent_path();
    GameContext c{world,physics,audio,state,ids,visuals,root,sounds};
    WorldPaths actors(c);
    assert(state.rng_calls==0);
    assert(actors.bound_entities().size()==33);
    const auto configured=actors.coverage();
    assert(configured.helicopters==1&&configured.trios==4&&configured.schools==2);
    assert(configured.active_helicopters==0&&configured.active_trios==0&&configured.active_schools==0);
    assert(configured.gated_entities==(enabled?0u:23u));
    assert(world_paths_source_owner("GMA_heli_pervane")=="GMA_heli_govde");
    assert(world_paths_source_owner("fish_m_shark1c")=="fish_m_shark1b");
    assert(world_paths_source_owner("hamsi_suru_2a")=="fish_path_hamsi2");
    if(enabled) {
        visuals[entity(world,"hamsi_suru_1a")].invisible=true;
        visuals[entity(world,"hamsi_suru_2a")].invisible=true;
    }
    actors.initialize(c,{v(.5),0,0});
    assert(state.rng_calls==0); // Enqueue-only startup never draws scales/phases or runs bodies.
    actors.update(c,{v(20),0,0});assert(state.rng_calls==0);
    const auto startup_draws=state.rng_calls;
    state.freeze_mode=v(1);actors.update(c,{v(20),1,0});assert(state.rng_calls==startup_draws);
    state.freeze_mode={};
    auto finite_rng=state;
    actors.update(c,{v(.5),1,0});
    assert(actors.coverage().active_helicopters==0&&state.rng_calls==(enabled?24u:3u));
    for(EntityId id=0;id<world.entities.size();++id) {
        const auto& action=world.entities[id].action;
        const bool sword=action.starts_with("fish_m_sword");
        const bool shark=action.starts_with("fish_m_shark");
        const bool school=action.starts_with("hamsi_suru_");
        if(!sword&&!shark&&!school)continue;
        if(!enabled&&!action.starts_with("fish_m_shark1"))continue;
        const auto raw_scale=school?(v(100)+source_random(finite_rng,v(60)))/v(100):
            v(7)*(v(sword?50:100)+source_random(finite_rng,v(sword?50:150)))/v(100);
        const auto& body=physics.body(id);const auto actual=body_source(body.scale);
        assert(actual.x==raw_scale&&actual.y==raw_scale&&actual.z==raw_scale);
        assert(property(world.entities[id].scale[0])==raw_scale);
        assert(body.angles.x==0&&body.angles.y==0&&body.angles.z==0);
    }
    assert(finite_rng.rng_state==state.rng_state&&finite_rng.rng_calls==state.rng_calls);
    auto phase_rng=state;
    std::array<Var,6> school_phase{};
    if(enabled) {
        for(int trio=0;trio<4;++trio) {
            (void)source_random(phase_rng,v(16));(void)source_random(phase_rng,v(17));(void)source_random(phase_rng,v(16));
        }
        constexpr int offsets[]{0,16,33,50,66,83};
        for(std::size_t i=0;i<6;++i)school_phase[i]=v(offsets[i])+source_random(phase_rng,v(i%2?9:8));
    }
    actors.update(c,{v(.5),2,0});
    assert(actors.coverage().active_helicopters==0); // Rotor resumes after body partner polling.
    actors.update(c,{v(.5),3,0});
    const auto active=actors.coverage();
    assert(active.active_helicopters==1&&active.active_trios==(enabled?4u:1u)&&active.active_schools==(enabled?2u:0u));
    const auto heli=entity(world,"GMA_heli_govde"),path=entity(world,"GMA_heli_path");
    // The actual animated path uses distinct vertex4 heading anchor and vertex3 body placement.
    const auto p3=body_source(physics.vec_for_vertex(path,3,visuals[path].sample));
    const auto p4=body_source(physics.vec_for_vertex(path,4,visuals[path].sample));
    const auto position=body_source(physics.body(heli).position);
    assert(position.x==p3.x&&position.y==p3.y&&position.z==p3.z);
    assert(p3.x!=p4.x||p3.y!=p4.y||p3.z!=p4.z);
    for(std::uint32_t index:{3u,4u}) {
        const auto vertex=c4::assets::sample_source_vertex(c.group(path),visuals[path].sample,index-1);
        const auto& pathbody=physics.body(path);
        const auto transformed=model_vertex_position({vertex.x,vertex.y,vertex.z},pathbody.position,pathbody.angles,pathbody.scale);
        const auto expected=source(transformed),actual=body_source(physics.vec_for_vertex(path,index,visuals[path].sample));
        assert(expected.x==actual.x&&expected.y==actual.y&&expected.z==actual.z);
    }
    const auto rotor=entity(world,"GMA_heli_pervane");
    assert(physics.body(rotor).position.x==physics.body(heli).position.x);
    assert(physics.body(rotor).angles.z==physics.body(heli).angles.z);
    const auto shark=entity(world,"fish_m_shark1a"),sharkpath=entity(world,"fish_path_shark1");
    auto sharkpose=visuals[shark].sample;const auto draws=state.rng_calls;
    auto reference_actors=actors;auto reference_visuals=visuals;auto reference_state=state;
    c4::physics::PhysicsWorld reference_physics(world);
    for(EntityId id=0;id<world.entities.size();++id)reference_physics.body(id)=physics.body(id);
    GameContext reference{world,reference_physics,audio,reference_state,ids,reference_visuals,root,sounds};
    visuals[shark].invisible=true;
    if(enabled) {
        visuals[entity(world,"hamsi_suru_1a")].invisible=true;
        visuals[entity(world,"hamsi_suru_2a")].invisible=true;
    }
    for(std::uint64_t frame=4;frame<10;++frame)actors.update(c,{v(.5),frame,0});
    sample_equal(sharkpose,visuals[shark].sample);assert(state.rng_calls==draws);
    state.freeze_mode=v(1);const auto helipose=visuals[heli].sample;
    actors.update(c,{v(20),10,0});sample_equal(helipose,visuals[heli].sample);
    state.freeze_mode=v(2);actors.update(c,{v(20),11,0});sample_equal(helipose,visuals[heli].sample);
    state.freeze_mode={};visuals[shark].invisible=false;
    actors.update(c,{v(.5),12,0});
    const auto sharkposition=body_source(physics.body(shark).position);
    reference_actors.update(reference,{v(.5),12,0});
    sample_equal(visuals[shark].sample,reference_visuals[shark].sample);
    const auto expected_shark=body_source(reference_physics.body(shark).position);
    assert(sharkposition.x==expected_shark.x&&sharkposition.y==expected_shark.y&&sharkposition.z==expected_shark.z);
    // Three source vertex4 positions are driven by actual MDL phases, not authored WMB position.
    assert(sharkposition.x!=body_source(physics.body(sharkpath).position).x||
           sharkposition.y!=body_source(physics.body(sharkpath).position).y);
    if(enabled) {
        // Raw timer boundary: fresh six RNG draws below100, target draws at exactly100,
        // strict >200 reset, with all operations retaining source Q10 order.
        auto& first=visuals[entity(world,"hamsi_suru_1a")];first.invisible=false;
        std::array<Var,6> timer{},speed{},target{},initial{};speed.fill(v(100));target.fill(v(100));initial.fill(v(100));
        auto expected=state;
        std::array<Var,6> travel{},swim{};
        const auto schoolpath=entity(world,"fish_path_hamsi1");
        constexpr std::array<std::string_view,6> fish_actions{
            "hamsi_suru_1a","hamsi_suru_1b","hamsi_suru_1c","hamsi_suru_1d","hamsi_suru_1e","hamsi_suru_1f"};
        for(std::uint64_t frame=13;frame<145;++frame) {
            std::array<c4::assets::AnimationSample,6> poses{};
            std::array<ScriptVec,6> positions{};
            for(std::size_t i=0;i<6;++i) {
                const auto fish=entity(world,fish_actions[i]);
                const auto sample=c4::assets::animation_sample(c.group(schoolpath),"Frame",
                    static_cast<float>(number(var_modulo(travel[i]+school_phase[i],v(100)))),true);
                positions[i]=body_source(physics.vec_for_vertex(schoolpath,4,sample));
                poses[i]=c4::assets::animation_sample(c.group(fish),"Frame",
                    static_cast<float>(number(var_modulo(swim[i]+school_phase[i],v(100)))),true);
                swim[i]=var_modulo(swim[i]+v(6.25)/v(2)*v(.5)*speed[i]/v(100),v(100));
                travel[i]=var_modulo(travel[i]+(v(6.25)/v(125))*v(.5)*speed[i]/v(100),v(100));
                if(timer[i]>=v(100)) {
                    if(timer[i]==v(100)){target[i]=v(100)-source_random(expected,v(70));initial[i]=speed[i];}
                    speed[i]=initial[i]+(target[i]-initial[i])*(timer[i]-v(100))/v(100);
                    timer[i]+=v(6.25)/v(.5)*v(.5);
                    if(timer[i]>v(200))timer[i]={};
                } else timer[i]=var_min(timer[i]+v(6.25)/(v(i<3?2:1)+source_random(expected,v(200))/v(100))*v(.5),v(100));
            }
            actors.update(c,{v(.5),frame,0});
            assert(state.rng_state==expected.rng_state&&state.rng_calls==expected.rng_calls);
            for(std::size_t i=0;i<6;++i) {
                const auto fish=entity(world,fish_actions[i]);
                sample_equal(visuals[fish].sample,poses[i]);
                const auto actual=body_source(physics.body(fish).position),wanted=positions[i];
                assert(actual.x==wanted.x&&actual.y==wanted.y&&actual.z==wanted.z);
            }
        }
    }
    actors.teardown();const auto final=visuals[heli].sample;
    const auto unloaded=actors.coverage();
    assert(unloaded.active_helicopters==0&&unloaded.active_trios==0&&unloaded.active_schools==0);
    actors.update(c,{v(.5),200,0});sample_equal(final,visuals[heli].sample);
}
}
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"usage: test_world_paths ORIGINAL_ASSET_ROOT\n";return 2;}
    const std::array<std::string_view,1> names{"bogazici.wmb"};
    const auto resources=c4::assets::resolve_resources(std::filesystem::path(argv[1])/"C4.wrs",names);
    paths(resources[0],false);paths(resources[0],true);
}
