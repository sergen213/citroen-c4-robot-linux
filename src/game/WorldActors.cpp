#include "WorldActors.hpp"
#include "physics/NativeNumeric.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace c4::game {
namespace {
using physics::v;
using physics::number;
constexpr std::uint32_t passable=0x200u, transparent=0x400u, shadow=0x40000u;
constexpr std::array<std::string_view,13> cycle_actions{
    "animate_hiz", "GMA_C4_BALON", "GMA_LOGO_BAL", "GMA_bayrak", "GMA_bayrak2_c4",
    "GMA_bayrak_drek", "GMA_bez_afis", "GMA_dolap", "GMA_marti", "GMA_park_ani",
    "GMA_pervane", "GMA_tent24", "GMA_tunel_atmos"};
constexpr std::array<std::string_view,16> facing_actions{
    "sadece_don", "GMA_ag_plane1", "GMA_ag_plane2", "GMA_ag_plane3", "GMA_over_cali_tek",
    "GMA_over_cali_tek2", "GMA_calim", "GMA_arka_a1", "GMA_arka_a2", "GMA_arka_a3",
    "GMA_arka_a4", "GMA_arka_a5", "GMA_arka_a6", "GMA_arka_a7", "GMA_arka_a8", "GMA_arka_a9"};
constexpr std::array<std::string_view,4> skin_actions{
    "GMA_start_lamba", "GMA_uc_ok", "GMA_ucok_av", "GMA_ucok_tun"};
template<std::size_t N> bool contains(const std::array<std::string_view,N>& names,std::string_view action) {
    return std::find(names.begin(),names.end(),action)!=names.end();
}
void flags(assets::WorldEntity& entity,std::uint32_t set,std::uint32_t clear=0) {
    entity.source_flags_set=(entity.source_flags_set|set)&~clear;
    entity.source_flags_clear=(entity.source_flags_clear|clear)&~set;
}
void facing_pose(GameContext& ctx,EntityId id) {
    auto& body=ctx.physics.body(id);
    auto angles=physics::var_vec_to_angle(ctx.state.camera_position-physics::body_source(body.position),
                                         physics::body_source(body.angles));
    angles.y=v(0); angles.z=v(0);
    body.angles=physics::native(angles);
}
void facing_wait(WorldActorWait& wait,const VisualState& visual,FrameTiming timing) {
    if(visual.invisible)wait.seconds(v(.5),timing);
    else wait.frame(timing);
}
} // namespace

void WorldActorWait::frame(FrameTiming timing) {
    remaining={}; yielded_frame=timing.frame_index;
}
void WorldActorWait::seconds(Var duration,FrameTiming timing) {
    // Proven module mode1 dispatches interpreted actions. wait10006450
    // precredits TIME_STEP; subsequent queue countdown uses raw TIME_FRAME.
    const auto dt=timing.originaltime_step_ticks;
    auto ticks=duration*v(16);
    if(ticks<dt)ticks=dt;
    else if(ticks>Var{1048575})ticks=Var{1048575};
    remaining=dt-ticks;
    yielded_frame=timing.frame_index;
}
bool WorldActorWait::resume(FrameTiming timing) {
    if(timing.frame_index==yielded_frame)return false;
    yielded_frame=timing.frame_index;
    if(remaining>=v(1)) { remaining-=v(1); return false; }
    const auto bound=physics::var_min(timing.originaltime_frame_ticks,v(1));
    if(remaining>-bound)return true;
    remaining+=timing.originaltime_frame_ticks;
    if(remaining>v(0))remaining=v(0);
    return false;
}

void prepare_world_actors(assets::WorldScene& world,SharedState& state,std::vector<VisualState>& visuals) {
    if(visuals.size()!=world.entities.size())throw std::logic_error("World actor preparation requires finalized visuals");
    // c4_effects14-170: defaults/flags are setup metadata, not a consumed frame.
    for(EntityId id=0;id<world.entities.size();++id) {
        auto& entity=world.entities[id]; auto& visual=visuals[id];
        const auto bits=(entity.flags|entity.source_flags_set)&~entity.source_flags_clear;
        if(contains(cycle_actions,entity.action)) {
            if(!(bits&1u))flags(entity,passable);
            if(entity.skills[0]==0)entity.skills[0]=1;
            if(bits&2u)entity.source_ambient=static_cast<float>(number(v(entity.skills[1])));
            if((bits&4u)&&entity.skills[2]!=0) {
                flags(entity,transparent);
                visual.transparent=true; visual.alpha=v(entity.skills[2]);
                entity.alpha_percent=static_cast<float>(number(visual.alpha));
            }
        }
        if(contains(facing_actions,entity.action)) {
            flags(entity,passable,shadow);
            entity.material="Mtl_agac_don";
            if(entity.action!="sadece_don") {
                flags(entity,0,transparent);
                visual.transparent=false; visual.alpha=v(100); entity.alpha_percent=100;
            }
        }
        if(contains(skin_actions,entity.action)||entity.action=="GMA_start_lamba_gl")visual.skin=1;
    }
    prepare_world_paths(world,state,visuals);
}

WorldActors::WorldActors(GameContext& ctx):leaves_(ctx),paths_(ctx),ambience_(ctx) {
    EntityId glow=no_entity;
    for(EntityId id=0;id<ctx.world.entities.size();++id)
        if(ctx.world.entities[id].action=="GMA_start_lamba_gl")glow=id;
    const auto path_entities=paths_.bound_entities();
    const auto tree_entities=leaves_.bound_entities();
    const auto leaf_entities=leaves_.leaf_entities();
    for(EntityId id=0;id<ctx.world.entities.size();++id) {
        const auto& entity=ctx.world.entities[id];
        if(contains(cycle_actions,entity.action)) {
            processes_.push_back({ProcessKind::cycle,id,cycles_.size()});
            const auto bits=(entity.flags|entity.source_flags_set)&~entity.source_flags_clear;
            cycles_.push_back({id,{},v(entity.skills[0]),{},entity.action!="animate_hiz",false,false,(bits&8u)!=0});
        } else if(contains(facing_actions,entity.action)) {
            processes_.push_back({ProcessKind::facing,id,facing_.size()});
            facing_.push_back({id,{},entity.action=="sadece_don",false});
        } else if(contains(skin_actions,entity.action)) {
            const bool lamp=entity.action=="GMA_start_lamba";
            processes_.push_back({ProcessKind::skin,id,skins_.size()});
            skins_.push_back({id,lamp?glow:no_entity,{},v(entity.skills[0]),{},1,lamp,false});
        } else if(entity.action=="GMA_loop_reklam_bez") {
            processes_.push_back({ProcessKind::banner,id,banners_.size()});
            banners_.push_back({id});
        } else if(entity.action=="water_yeni") {
            processes_.push_back({ProcessKind::water,id,water_.size()});
            water_.push_back({id});
        } else if(!entity.source_action_material_before_wait.empty()) {
            processes_.push_back({ProcessKind::material,id,materials_.size()});
            materials_.push_back({id});
        } else if(std::find(path_entities.begin(),path_entities.end(),id)!=path_entities.end()) {
            processes_.push_back({ProcessKind::path,id,0});
        } else if(std::find(tree_entities.begin(),tree_entities.end(),id)!=tree_entities.end()) {
            processes_.push_back({ProcessKind::leaf,id,0,false});
        } else if(std::find(leaf_entities.begin(),leaf_entities.end(),id)!=leaf_entities.end()) {
            processes_.push_back({ProcessKind::leaf,id,0,true});
        }
    }
    source_queue_.reserve(processes_.size());
    destination_queue_.reserve(processes_.size());
    entity_to_process_.assign(ctx.world.entities.size(),std::numeric_limits<std::size_t>::max());
    for(std::size_t index=0;index<processes_.size();++index)entity_to_process_[processes_[index].id]=index;
}

void WorldActors::initialize(GameContext& ctx,FrameTiming timing) {
    if(active_)return;
    active_=true;last_frame_=timing.frame_index;
    source_queue_.clear();destination_queue_.clear();
    for(std::size_t index=0;index<processes_.size();++index)
        if(!processes_[index].staged_leaf)source_queue_.push_back(index);
    for(auto& actor:cycles_) {
        actor.frame={};actor.started=false;actor.entered=false;
        actor.wait.frame(timing);
    }
    for(auto& actor:facing_) {
        actor.started=false;actor.entered=false;
        actor.wait.frame(timing);
    }
    for(auto& actor:skins_) {
        actor.frame={};actor.skin=1;actor.started=false;actor.entered=false;actor.wait.frame(timing);
    }
    for(auto& actor:banners_) {
        actor.frame={};actor.reverse=false;actor.stage=BannerStage::entry;actor.wait.frame(timing);
    }
    for(auto& actor:water_){actor.entered=false;actor.wait.frame(timing);}
    for(auto& actor:materials_){actor.entered=false;actor.wait.frame(timing);}
    leaves_.initialize(ctx,timing);
    paths_.initialize(ctx,timing);
    ambience_.initialize(ctx,timing);
}
void WorldActors::teardown(GameContext& ctx) {
    if(!active_)return;
    active_=false;
    leaves_.teardown(ctx);
    paths_.teardown();
    ambience_.teardown(ctx);
}
void WorldActors::cycle(GameContext& ctx,Cycle& actor,FrameTiming timing) {
    if(!actor.entered) {
        actor.entered=true;
        if(actor.wrapper){actor.wait.frame(timing);return;}
    }
    if(!actor.started&&actor.random_on)actor.frame=ctx.random(v(100));
    actor.started=true;
    if(!ctx.visuals[actor.id].invisible)ctx.animate(actor.id,"frame",actor.frame,true);
    actor.frame=physics::var_modulo(actor.frame+v(6.25)/actor.speed*timing.originaltime_step_ticks,v(100));
    actor.wait.frame(timing);
}
void WorldActors::face(GameContext& ctx,Facing& actor,FrameTiming timing) {
    if(!actor.entered) {
        actor.entered=true;
        if(!actor.immediate){actor.wait.frame(timing);return;}
        ctx.set_material(actor.id,"Mtl_agac_don");
        actor.started=true;
    }
    if(!actor.started) {
        ctx.set_material(actor.id,"Mtl_agac_don");
        actor.started=true;
        facing_wait(actor.wait,ctx.visuals[actor.id],timing);
        return; // wrapper's wait1, then helper's wait/sleep BEFORE first bearing
    }
    facing_pose(ctx,actor.id);
    if(actor.immediate)actor.wait.frame(timing);
    else facing_wait(actor.wait,ctx.visuals[actor.id],timing);
}
void WorldActors::skin(GameContext& ctx,Skin& actor,FrameTiming timing) {
    if(!actor.entered){actor.entered=true;actor.wait.frame(timing);return;}
    if(actor.lamp&&actor.partner==no_entity){actor.wait.frame(timing);return;}
    if(!actor.started) {
        actor.started=true;actor.skin=1;
        ctx.visuals[actor.id].skin=1;
        if(actor.partner!=no_entity)ctx.visuals[actor.partner].skin=1;
    }
    // c4_effects555/577 checks strictly BEFORE increment; no modulo.
    if(actor.frame>v(100)) {
        actor.skin=actor.skin==1?2:1;actor.frame={};
        ctx.visuals[actor.id].skin=actor.skin;
        if(actor.partner!=no_entity)ctx.visuals[actor.partner].skin=actor.skin;
    }
    actor.frame+=v(6.25)/actor.speed*timing.originaltime_step_ticks;
    actor.wait.frame(timing);
}
void WorldActors::banner(GameContext& ctx,Banner& actor,FrameTiming timing) {
    if(actor.stage==BannerStage::entry) {
        actor.stage=BannerStage::startup;actor.wait.frame(timing);return;
    }
    if(actor.stage==BannerStage::startup) {
        actor.stage=BannerStage::delay;actor.wait.seconds(v(4),timing);return;
    }
    if(actor.stage==BannerStage::delay) {
        actor.frame=actor.reverse?v(100):v(0);actor.stage=BannerStage::moving;
    }
    if((!actor.reverse&&actor.frame>v(100))||(actor.reverse&&actor.frame<v(0))) {
        ctx.animate(actor.id,"frame",actor.reverse?v(0):v(100),false);
        actor.reverse=!actor.reverse;actor.stage=BannerStage::delay;
        actor.wait.seconds(v(4),timing);return;
    }
    ctx.animate(actor.id,"frame",actor.frame,false);
    const auto increment=v(6.25)/v(3)*timing.originaltime_step_ticks;
    actor.frame=actor.reverse?actor.frame-increment:actor.frame+increment;
    actor.wait.frame(timing);
}
void WorldActors::update(GameContext& ctx,FrameTiming timing) {
    if(!active_||last_frame_==timing.frame_index)return;
    last_frame_=timing.frame_index;
    if(ctx.state.freeze_mode>=v(2))return;
    // Global HUD continuation has savedmy==0: freeze1 still stops/waits its voices.
    ambience_.update(ctx,timing);
    if(ctx.state.freeze_mode>=v(1))return;
    // These authored actions have no active proc_early/late flags: one normal
    // FIFO traversal, with every carried/yielded record tail-appended separately.
    // Never visit newly yielded destination work in this source traversal.
    destination_queue_.clear();
    for(const auto index:source_queue_)
        if(dispatch(ctx,processes_[index],timing))destination_queue_.push_back(index);
    source_queue_.swap(destination_queue_);
}

bool WorldActors::dispatch(GameContext& ctx,const Process& process,FrameTiming timing) {
    switch(process.kind) {
    case ProcessKind::cycle: {
        auto& actor=cycles_[process.local];
        if(actor.wait.resume(timing))cycle(ctx,actor,timing);
        return true;
    }
    case ProcessKind::facing: {
        auto& actor=facing_[process.local];
        if(actor.wait.resume(timing))face(ctx,actor,timing);
        return true;
    }
    case ProcessKind::skin: {
        auto& actor=skins_[process.local];
        if(actor.wait.resume(timing))skin(ctx,actor,timing);
        return true;
    }
    case ProcessKind::banner: {
        auto& actor=banners_[process.local];
        if(actor.wait.resume(timing))banner(ctx,actor,timing);
        return true;
    }
    case ProcessKind::water: {
        auto& actor=water_[process.local];
        if(!actor.wait.resume(timing))return true;
        if(!actor.entered){actor.entered=true;actor.wait.frame(timing);return true;}
        if(ctx.world.source_profile.shq>=1||ctx.world.source_profile.mtq==2) {
            // effect_define247-251: ordered shared matrix11/22=float40,
            // skin1=MTI_denz_nor and effect_load("water_entskin.fx") precede
            // this ENTITY setter. The bridge commits that exact source recipe
            // from this persistent revision, never from identity or visibility.
            ++ctx.state.source_water_material_write_revision;
            ctx.set_material(actor.id,"mtl_denizwater");
        }
        return false; // water_yeni returns; MATERIAL callbacks are bridge-owned.
    }
    case ProcessKind::material: {
        auto& actor=materials_[process.local];
        if(!actor.wait.resume(timing))return true;
        if(actor.entered)return false; // source wait1 completed; static action returns
        ctx.set_material(actor.id,ctx.world.entities[actor.id].source_action_material_before_wait);
        actor.entered=true;actor.wait.frame(timing);
        return true;
    }
    case ProcessKind::path:return paths_.dispatch_entity(ctx,timing,process.id);
    case ProcessKind::leaf: {
        const bool retained=leaves_.dispatch_entity(ctx,timing,process.id);
        for(const auto id:leaves_.spawned_entities())
            destination_queue_.push_back(entity_to_process_.at(id));
        return retained;
    }
    }
    throw std::logic_error("Unknown typed world continuation");
}

EntityId WorldActors::queued_entity(std::size_t ordinal) const {
    return processes_.at(source_queue_.at(ordinal)).id;
}

WorldActors::Coverage WorldActors::coverage() const {
    Coverage result;
    result.queued=active_;
    result.source_queue_size=active_?source_queue_.size():0;
    if(active_&&!source_queue_.empty())result.first_queued_id=processes_[source_queue_.front()].id;
    result.cycles=cycles_.size();result.facing=facing_.size();
    result.skins=skins_.size();result.banners=banners_.size();
    result.water=water_.size();result.materials=materials_.size();
    result.paths=paths_.coverage();
    result.leaves=leaves_.coverage();
    if(active_) {
        for(const auto& actor:cycles_){result.active_cycles+=actor.started;result.pending_entries+=!actor.entered;}
        for(const auto& actor:facing_){result.active_facing+=actor.started;result.pending_entries+=!actor.entered;}
        for(const auto& actor:skins_){result.active_skins+=actor.started;result.pending_entries+=!actor.entered;}
        for(const auto& actor:banners_) {
            result.active_banners+=actor.stage==BannerStage::delay||actor.stage==BannerStage::moving;
            result.pending_entries+=actor.stage==BannerStage::entry;
        }
        for(const auto& actor:water_)result.pending_entries+=!actor.entered;
        for(const auto& actor:materials_)result.pending_entries+=!actor.entered;
        result.started=result.pending_entries<cycles_.size()+facing_.size()+skins_.size()+banners_.size()+water_.size()+materials_.size();
    }
    if(!cycles_.empty()){result.cycle_id=cycles_.front().id;result.cycle_frame=cycles_.front().frame;}
    if(!skins_.empty()){result.skin_id=skins_.front().id;result.skin_frame=skins_.front().frame;result.skin_number=skins_.front().skin;}
    if(!banners_.empty()) {
        const auto& actor=banners_.front();
        result.banner_id=actor.id;result.banner_frame=actor.frame;
        result.banner_wait=actor.wait.remaining;result.banner_reverse=actor.reverse;
    }
    return result;
}
} // namespace c4::game
