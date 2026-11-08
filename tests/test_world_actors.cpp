#include "game/WorldActors.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>

using namespace c4;
using physics::v;
using physics::number;

int main(int argc, char** argv) {
    // Resolve the actual gameplay WMB through the original asset-root catalog.
    if (argc != 2) { std::cerr << "Usage: test_world_actors <original-asset-root>\n"; return 2; }
    const std::filesystem::path root=argv[1];
    constexpr std::array<std::string_view,1> names{"bogazici.wmb"};
    const auto resource=assets::resolve_resources(root/"C4.wrs",names,assets::original_source_profile).at(0);
    auto world=assets::load_world(resource,assets::original_source_profile);
    game::SharedState state;
    state.particle_count = v(0);
    std::vector<game::VisualState> visuals(world.entities.size());
    const auto initial_rng=state.rng_state;
    auto ids=game::bootstrap_game(world,root,state,visuals);
    assert(state.rng_calls==0 && state.rng_state==initial_rng);
    for(const auto& leaf:state.world_leaf_startup)assert(visuals[leaf.entity].invisible);
    auto find = [&](std::string_view action) {
        for (game::EntityId id=0; id<world.entities.size(); ++id)
            if (world.entities[id].action == action) return id;
        throw std::runtime_error("Missing actual source action: " + std::string(action));
    };
    const auto skin=find("GMA_uc_ok"), cycle=find("GMA_tunel_atmos"), facing=find("GMA_ag_plane1"), static_banner=find("GMA_loop_reklam_bez");
    const auto water=find("water_yeni");
    const auto direct=find("sadece_don");
    // Direct source writes are not WMB/editor identities or helper MATERIAL names.
    constexpr std::array<std::pair<std::string_view,std::string_view>,19> direct_materials{{
        {"GMA_anadolu","mat_multi_anadolu"}, {"GMA_avrupa","mat_multi_avrupa"},
        {"SHM_anayol","Mtl_SH_anayol"}, {"SHM_beton","Mtl_SH_beton"},
        {"SHM_cikis","Mtl_cikis"}, {"SHM_denekop_ek","Mtl_SH_denekop_ek"},
        {"SHM_ex_yol","Mtl_SH_ex_yol"}, {"SHM_ex_yol_kald","Mtl_ex_yol_kald"},
        {"SHM_gecis","Mtl_SH_gecis"}, {"SHM_kule","Mtl_SH_kule"},
        {"SHM_kule2","Mtl_SH_kule2"}, {"SHM_parca_2","Mtl_parca_2"},
        {"SHM_tup_kald","Mtl_tup_kald"}, {"SHM_tup_yol","Mtl_tup_yol"},
        {"SHM_vadi","Mtl_vadi"}, {"SHM_vadi_yan","Mtl_vadi_yan"},
        {"SHM_y_kum_yan","Mtl_y_kum_yan"}, {"SHM_yol_kum","Mtl_yol_kum"},
        {"SHM_zgisealt","Mtl_SH_zgisealt"}
    }};
    std::size_t imported_materials=0;
    for(const auto& [action,material]:direct_materials) {
        const auto id=find(action);
        const auto& entity=world.entities[id];
        assert(entity.source_initial_material_known);
        assert(entity.source_initial_material_identity!=material);
        assert(entity.source_action_material_before_wait==material);
        imported_materials+=!world.models[entity.model_index].imported_resource.empty();
        // Same birth publication as GameSession firstPlay, before queued actions run.
        visuals[id].material_identity=entity.source_initial_material_identity;
        visuals[id].material_identity_known=entity.source_initial_material_known;
    }
    assert(imported_materials==15);
    assert(world.entities[find("GMA_BEKO")].source_action_material_before_wait.empty());
    assert(world.entities[facing].source_action_material_before_wait.empty());
    assert(world.entities[water].source_action_material_before_wait.empty());
    const auto assert_materials=[&](bool published) {
        for(const auto& [action,material]:direct_materials) {
            const auto id=find(action);
            assert(visuals[id].material_identity_known);
            assert(visuals[id].material_identity==(published?material:world.entities[id].source_initial_material_identity));
        }
    };
    assert_materials(false);
    // Make the actual authored skin reach exactly 100 on its first helper body.
    world.entities[skin].skills[0]=1;
    physics::PhysicsWorld physics(world);
    audio::Audio audio(48000,audio::Mode::Offline);
    game::SoundCache sounds;
    game::GameContext ctx{world,physics,audio,state,ids,visuals,root,sounds};
    auto banner=game::no_entity;
    for(game::EntityId id=0;id<world.entities.size();++id)
        if(world.entities[id].action=="GMA_loop_reklam_bez" && ctx.group(id).frames.size()>1) {
            banner=id;
            break;
        }
    assert(banner!=game::no_entity && ctx.group(static_banner).frames.size()==1);
    const auto same_sample=[](const assets::AnimationSample& a,const assets::AnimationSample& b) {
        return a.first_frame==b.first_frame && a.second_frame==b.second_frame && a.blend==b.blend
            && a.frame_raw==b.frame_raw && a.next_frame_raw==b.next_frame_raw;
    };
    game::WorldActors actors(ctx);
    assert(state.rng_calls==0 && state.rng_state==initial_rng);
    // Exact current-identity API: unknown differs from a known source NULL.
    const auto initial_water_material=visuals[water].material_identity;
    const auto initial_water_material_known=visuals[water].material_identity_known;
    ctx.set_material(water,{});
    assert(visuals[water].material_identity_known && visuals[water].material_identity.empty());
    ctx.set_material(water,"Mtl_agac_don");
    ctx.set_material(water,"mtl_denizwater");
    ctx.set_material(water,"Mtl_agac_don"); // A->B->A before reconciliation leaves only A.
    assert(visuals[water].material_identity=="Mtl_agac_don");
    assert(state.source_water_material_write_revision==0);
    assert(state.rng_calls==0 && state.rng_state==initial_rng);
    visuals[water].material_identity=initial_water_material;
    visuals[water].material_identity_known=initial_water_material_known;
    ctx.set_material(facing,{}); // Explicit test setup; wrapper must not publish early.
    ctx.set_material(direct,{});
    const auto bound=actors.coverage();
    assert(!bound.started && bound.active_cycles==0 && bound.active_facing==0);
    assert(bound.cycles==75 && bound.facing==440 && bound.skins==4 && bound.banners==4);
    assert(bound.materials==direct_materials.size());
    game::FrameTiming timing;
    timing.originaltime_step_ticks=v(16);
    timing.originaltime_frame_ticks=v(1);
    assert(state.rng_calls==0 && state.rng_state==initial_rng);
    actors.initialize(ctx,timing);
    assert(state.rng_calls==0 && state.rng_state==initial_rng);
    assert_materials(false); // enqueue is not source execution
    actors.update(ctx,timing);
    assert_materials(false); // same source frame cannot enter queued actions
    std::vector<game::EntityId> placement_order;
    for(game::EntityId id=0;id<world.entities.size();++id) {
        const auto& action=world.entities[id].action;
        const bool core=action!="GMA_start_lamba_gl"&&std::any_of(game::world_actor_source_owners.begin(),game::world_actor_source_owners.end(),
            [&](const auto& binding){return binding.action==action;});
        const bool tree=std::any_of(state.world_leaf_startup.begin(),state.world_leaf_startup.end(),
            [&](const auto& leaf){return leaf.parent==id;});
        if(core||tree||!world.entities[id].source_action_material_before_wait.empty()
            ||!game::world_paths_source_owner(action).empty())placement_order.push_back(id);
    }
    assert(actors.coverage().source_queue_size==placement_order.size());
    for(std::size_t ordinal=0;ordinal<placement_order.size();++ordinal)
        assert(actors.queued_entity(ordinal)==placement_order[ordinal]);
    const auto assert_fifo=[&] {
        auto expected=placement_order.begin();
        for(std::size_t ordinal=0;ordinal<actors.coverage().source_queue_size;++ordinal) {
            expected=std::find(expected,placement_order.end(),actors.queued_entity(ordinal));
            assert(expected!=placement_order.end());
            ++expected;
        }
    };
    const auto queued=actors.coverage();
    assert(queued.queued && !queued.started && queued.pending_entries!=0);
    const auto untouched=visuals[cycle].sample;
    state.freeze_mode=v(1);
    ++timing.frame_index; actors.update(ctx,timing);
    assert(actors.coverage().pending_entries==queued.pending_entries);
    assert(visuals[cycle].sample.first_frame==untouched.first_frame && visuals[cycle].sample.blend==untouched.blend);
    assert(state.rng_calls==0 && state.rng_state==initial_rng);
    assert(visuals[facing].material_identity.empty());
    assert(visuals[direct].material_identity.empty());
    assert_materials(false); // freeze1 holds the complete source queue
    state.freeze_mode=v(0);
    ++timing.frame_index; actors.update(ctx,timing); // first entry: morph, then wait1
    assert(visuals[skin].skin==1 && actors.coverage().skin_frame==v(0));
    assert(actors.coverage().started && actors.coverage().pending_entries==0);
    assert_fifo();
    assert(visuals[skin].skin==1); // Initial enqueue and wrapper wait do not consume TIME.
    assert(visuals[facing].material_identity.empty()); // ent_morph wrapper still waits1.
    assert(visuals[direct].material_identity=="Mtl_agac_don"); // actual direct sprite has no wrapper wait
    assert_materials(true); // exact direct writes precede each action's first wait1
    std::vector<game::EntityId> after_birth;
    for(std::size_t ordinal=0;ordinal<actors.coverage().source_queue_size;++ordinal) {
        const auto id=actors.queued_entity(ordinal);
        bool tree=false;
        for(const auto& leaf:state.world_leaf_startup)
            if(leaf.parent==id){after_birth.push_back(leaf.entity);tree=true;}
        if(!tree&&world.entities[id].action!="water_yeni"&&world.entities[id].action!="GMA_heli_pervane"
            &&world.entities[id].source_action_material_before_wait.empty())
            after_birth.push_back(id);
    }
    ++timing.frame_index;
    actors.update(ctx,timing);
    assert(visuals[skin].skin==1); // helper increments to exactly100, no toggle
    assert(actors.coverage().source_queue_size==after_birth.size());
    for(std::size_t ordinal=0;ordinal<after_birth.size();++ordinal)
        assert(actors.queued_entity(ordinal)==after_birth[ordinal]);
    assert(actors.coverage().leaves.born==state.world_leaf_startup.size());
    assert(actors.coverage().leaves.initialized==0); // destination callbacks not revisited
    assert(visuals[facing].material_identity=="Mtl_agac_don"); // assignment before helper's first wait
    assert_materials(true); // finite material continuations finish after wait1
    assert(visuals[water].material_identity==initial_water_material);
    assert(visuals[water].material_identity_known==initial_water_material_known);
    const auto facing_before=physics.body(facing).angles;
    state.camera_position=physics::body_source(physics.body(facing).position)+game::ScriptVec{v(0),v(100),v(0)};
    ++timing.frame_index;
    actors.update(ctx,timing);
    assert(visuals[skin].skin==1); // strict >100 tested before increment
    assert(physics.body(facing).angles.x != facing_before.x);
    ++timing.frame_index;
    actors.update(ctx,timing);
    assert(visuals[skin].skin==2);
    assert(actors.coverage().cycle_id==cycle);
    auto expected_cycle=actors.coverage().cycle_frame;
    const auto cycle_step=v(6.25)/v(world.entities[cycle].skills[0]);
    timing.originaltime_step_ticks=v(.5);
    visuals[cycle].invisible=true;
    const auto hidden_sample=visuals[cycle].sample;
    for(int i=0;i<5;++i)expected_cycle=physics::var_modulo(expected_cycle+cycle_step*v(.5),v(100));
    for(int i=0;i<5;++i) { ++timing.frame_index; actors.update(ctx,timing); }
    assert(same_sample(visuals[cycle].sample,hidden_sample));
    visuals[cycle].invisible=false;
    ++timing.frame_index; actors.update(ctx,timing);
    const auto resumed=visuals[cycle].sample;
    const auto expected_sample=assets::animation_sample(ctx.group(cycle),"frame",static_cast<float>(number(expected_cycle)),true);
    assert(resumed.first_frame==expected_sample.first_frame && resumed.second_frame==expected_sample.second_frame && resumed.blend==expected_sample.blend);
    const auto frozen=visuals[cycle].sample;
    const auto frozen_banner=visuals[banner].sample;
    const auto frozen_locals=actors.coverage();
    state.freeze_mode=v(1);
    for(int i=0;i<10;++i) { ++timing.frame_index; actors.update(ctx,timing); }
    assert(visuals[cycle].sample.first_frame==frozen.first_frame && visuals[cycle].sample.blend==frozen.blend);
    assert(same_sample(visuals[banner].sample,frozen_banner));
    assert(actors.coverage().cycle_frame.raw==frozen_locals.cycle_frame.raw);
    assert(actors.coverage().skin_frame.raw==frozen_locals.skin_frame.raw);
    assert(actors.coverage().banner_wait.raw==frozen_locals.banner_wait.raw);
    assert(state.source_water_material_write_revision==0); // All0 branch performs no shared writes.
    state.freeze_mode=v(2);
    for(int i=0;i<5;++i){++timing.frame_index;actors.update(ctx,timing);}
    assert(same_sample(visuals[banner].sample,frozen_banner));
    assert(actors.coverage().cycle_frame.raw==frozen_locals.cycle_frame.raw);
    assert(actors.coverage().banner_wait.raw==frozen_locals.banner_wait.raw);
    actors.initialize(ctx,timing); // retained-world menu/restart must not restart locals
    assert(actors.coverage().cycle_frame.raw==frozen_locals.cycle_frame.raw);
    assert_materials(true); // retained-world restart does not replay action entry
    state.freeze_mode=v(0);
    ++timing.frame_index; actors.update(ctx,timing);
    expected_cycle=physics::var_modulo(expected_cycle+cycle_step*v(.5),v(100));
    const auto thaw_sample=assets::animation_sample(ctx.group(cycle),"frame",static_cast<float>(number(expected_cycle)),true);
    assert(visuals[cycle].sample.first_frame==thaw_sample.first_frame && visuals[cycle].sample.blend==thaw_sample.blend);
    visuals[facing].invisible=true;
    timing.originaltime_frame_ticks=v(1);
    ++timing.frame_index;actors.update(ctx,timing); // rotate then queue sleep(.5)
    const auto sleeping_pose=physics.body(facing).angles;
    state.camera_position=physics::body_source(physics.body(facing).position)+game::ScriptVec{v(100),v(0),v(0)};
    for(int i=0;i<7;++i){++timing.frame_index;actors.update(ctx,timing);}
    assert(physics.body(facing).angles.x==sleeping_pose.x);
    ++timing.frame_index;actors.update(ctx,timing);
    assert(physics.body(facing).angles.x==0 && physics.body(facing).angles.y==0 && physics.body(facing).angles.z==0);
    actors.teardown(ctx);
    timing.frame_index=1000;
    actors.initialize(ctx,timing);
    assert_materials(true); // explicit teardown/reinitialize does not overwrite current MATERIAL
    const auto banner_initial=visuals[banner].sample;
    const auto static_banner_initial=visuals[static_banner].sample;
    timing.originaltime_step_ticks=v(1);
    ++timing.frame_index; actors.update(ctx,timing); // queued first entry -> wrapper wait1
    ++timing.frame_index; actors.update(ctx,timing); // wrapper wait1 -> wait(-4)
    for(int i=0;i<63;++i) { ++timing.frame_index; actors.update(ctx,timing); }
    assert(same_sample(visuals[banner].sample,banner_initial));
    assert(same_sample(visuals[static_banner].sample,static_banner_initial));
    const auto endpoint0=assets::animation_sample(ctx.group(banner),"frame",0,false);
    const auto static_pose=assets::animation_sample(ctx.group(static_banner),"frame",0,false);
    ++timing.frame_index; actors.update(ctx,timing); // exact64ticks seconds wait resumes at loc_i0
    assert(same_sample(visuals[banner].sample,endpoint0));
    assert(same_sample(visuals[static_banner].sample,static_pose));
    ++timing.frame_index; actors.update(ctx,timing); // sample before the second Q10 increment
    const auto banner_step=v(6.25)/v(3)*timing.originaltime_step_ticks;
    const auto moving_sample=assets::animation_sample(ctx.group(banner),"frame",static_cast<float>(number(banner_step)),false);
    assert(!same_sample(moving_sample,endpoint0));
    assert(same_sample(visuals[banner].sample,moving_sample));
    assert_materials(true); // exact identities survive later source passes
    assert(same_sample(visuals[static_banner].sample,static_pose));
    game::WorldActorWait wait;
    wait.seconds(v(.5),timing);
    for(int i=0;i<7;++i) { ++timing.frame_index; assert(!wait.resume(timing)); }
    ++timing.frame_index; assert(wait.resume(timing));
    timing.originaltime_step_ticks=v(48);
    ++timing.frame_index;actors.update(ctx,timing);
    timing.originaltime_step_ticks=v(1); // terminal body enqueues wait(-4) with one tick credit
    ++timing.frame_index;actors.update(ctx,timing);
    const auto endpoint100=assets::animation_sample(ctx.group(banner),"frame",100,false);
    assert(same_sample(visuals[banner].sample,endpoint100));
    for(int i=0;i<64;++i){++timing.frame_index;actors.update(ctx,timing);}
    timing.originaltime_step_ticks=v(48);
    for(int i=0;i<2;++i){++timing.frame_index;actors.update(ctx,timing);}
    assert(same_sample(visuals[banner].sample,endpoint0));
    assert(same_sample(visuals[static_banner].sample,static_pose));
    auto unequal_timing=timing;
    unequal_timing.originaltime_step_ticks=v(2);
    unequal_timing.originaltime_frame_ticks=v(1);
    game::WorldActorWait interpreted;
    interpreted.seconds(v(4),unequal_timing);
    assert(interpreted.remaining==v(-62)); // interpreted TIME_STEP, not compiled TIME_FRAME credit
    for(int i=0;i<62;++i){++unequal_timing.frame_index;assert(!interpreted.resume(unequal_timing));}
    ++unequal_timing.frame_index;assert(interpreted.resume(unequal_timing));
    wait.frame(timing);
    assert(!wait.resume(timing));
    timing.originaltime_step_ticks=v(0);
    ++timing.frame_index; assert(wait.resume(timing)); // frame wait != TIME_STEP
    // Exercise the same actual water roots under their enabled source predicate.
    // This explicit fixture reactivation is not a menu/restart lifecycle model.
    actors.teardown(ctx);
    world.source_profile.shq=1;
    const auto before_water_write=state.source_water_material_write_revision;
    actors.initialize(ctx,timing);
    ++timing.frame_index;actors.update(ctx,timing); // initial body morph -> wait1
    assert(state.source_water_material_write_revision==before_water_write);
    ++timing.frame_index;actors.update(ctx,timing); // ordered source writes, then ENTITY setter
    assert(state.source_water_material_write_revision==before_water_write+bound.water);
    assert(visuals[water].material_identity_known && visuals[water].material_identity=="mtl_denizwater");
    const auto after_water_write=state.source_water_material_write_revision;
    actors.initialize(ctx,timing); // retained once-world guard does not create another writer
    for(int i=0;i<3;++i){++timing.frame_index;actors.update(ctx,timing);}
    assert(state.source_water_material_write_revision==after_water_write);
    actors.teardown(ctx);
    assert(state.source_water_material_write_revision==after_water_write); // revision is not event lifetime
}
