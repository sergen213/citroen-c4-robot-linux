#include "RaceLifecycle.hpp"
#include <stdexcept>

namespace c4::game {
using physics::v;
using physics::number;
bool turbo_pickup_contains(Vec3 body_position, Vec3 body_angles, Vec3 pickup_position) {
    auto target=physics::var_vec_scale(physics::body_source(body_position),v(0.001));
    auto source=physics::var_vec_scale(physics::body_source(pickup_position),v(0.001));
    if(physics::var_vec_dist(source,target)>=v(0.250)) return false;
    const auto body_euler=physics::body_source(body_angles);
    const ScriptVec angles={-body_euler.x,-body_euler.y,-body_euler.z};
    target=physics::var_vec_rotate(target,angles);
    source=physics::var_vec_rotate(source,angles);
    const auto delta=source-target;
    return physics::var_abs(delta.x)<v(0.1) &&
           physics::var_abs(delta.y)<v(0.06) && physics::var_abs(delta.z)<v(0.06);
}

void CountdownPickups::initialize(GameContext& ctx) {
    pickups_.clear();
    pickups_.reserve(ctx.ids.pickups.size());
    // initialize_game_physics executes turbo_icon_don's downward trace once,
    // before this context is constructed; do not raise an already-grounded icon.
    for(const auto id:ctx.ids.pickups) {
        ctx.physics.body(id).flags.passable=true;
        ctx.visuals.at(id).invisible=false;
        pickups_.push_back({id});
    }
    constexpr std::array<std::string_view,6> actions{
        "fish_m_shark1a","fish_m_shark1b","fish_m_shark1c",
        "fish_m_shark2a","fish_m_shark2b","fish_m_shark2c"};
    sharks_.fill(no_entity);
    for(EntityId id=0;id<ctx.world.entities.size();++id)
        for(std::size_t index=0;index<actions.size();++index)
            if(ctx.world.entities[id].action==actions[index]) sharks_[index]=id;
    for(const auto id:sharks_)
        if(id==no_entity) throw std::runtime_error("Race lifecycle: original shark action is missing");
}

void CountdownPickups::reset(GameContext& ctx, bool first) {
    auto& state=ctx.state;
    state.freeze_mode=v(0);
    state.ghost_reset_ok=v(1);
    state.ghost_yaris_start=v(0);
    state.start_turbo=v(0);
    state.black_alpha=v(100);
    state.black_visible=true;
    state.phase=GamePhase::countdown;
    stage_=Stage::initial_waits;
    frame_waits_=first?7:5; // First-only waits: source lines 213 and 225.
    progress_=charge_=retract_progress_=v(0);
    deadline_=0;
    red_skin_=charge_sample_=0;
    boost_played_=retract_raising_=false;
    // A reset cancels the old lift loop, but its source tail still rescales fish.
    // Leave that coroutine live until its next scheduled frame resumes.
}

void CountdownPickups::countdown(GameContext& ctx, const InputSnapshot& input,
                                const FrameTiming& timing) {
    const Var time=timing.originaltime_step_ticks;
    const double now=timing.seconds_deadlines;
    const auto entity=ctx.ids.p_zamanlama;
    auto skin=[&](int value) { ctx.visuals.at(entity).skin=value; };
    // Each return is the original coroutine wait. Negative waits use the
    // scheduler's real deadline clock, not time (the animation/freeze clock).
    switch(stage_) {
    case Stage::idle:
    case Stage::started:
        return;
    case Stage::initial_waits:
        if(--frame_waits_>0) return;
        ctx.state.game_over_ok=v(0);
        ctx.state.gameover_panel_acik=v(0);
        ctx.state.ghost_reset_ok=v(0);
        stage_=Stage::reset_wait;
        return;
    case Stage::reset_wait: {
        const bool black=ctx.state.c4_texture_rengi==v(4)||ctx.state.c4_texture_rengi==v(8);
        const bool grey=!black&&ctx.state.c4_texture_rengi==v(2);
        ctx.set_material(ctx.ids.p_arac_kaporta,black?"Mtl_siyah_car":grey?"Mtl_kul_car":"Mtl_car3");
        ctx.set_material(ctx.ids.p_robot_kaporta,black?"Mtl_siyah_robot":grey?"Mtl_kul_robot":"Mtl_robot_kaporta");
        if(ctx.state.bloom_fx_ok==v(0)) {
            ctx.state.bloom_fx_ok=v(1);
            ctx.state.gamma_corrected_active=true; // Source bloom_acil gamma call.
        }
        ctx.animate(entity,"indir",v(0));
        skin(1);
        stage_=Stage::timing_wait;
        return;
    }
    case Stage::timing_wait:
        ctx.state.black_visible=true;
        ctx.state.black_alpha=v(100);
        progress_=v(0);
        stage_=Stage::fade;
        break;
    case Stage::fade:
        break;
    case Stage::music_wait:
        if(now<deadline_) return;
        ctx.sound("ses_zaman_mekanik1");
        ctx.animate(entity,"indir",v(0));
        progress_=v(0);
        stage_=Stage::lower;
        break;
    case Stage::lower:
        break;
    case Stage::lower_wait:
        if(now<deadline_) return;
        red_skin_=2;
        skin(red_skin_);
        ctx.sound("ses_zaman_red");
        deadline_=now+number(v(1));
        stage_=Stage::red_wait;
        return;
    case Stage::red_wait:
        if(now<deadline_) return;
        skin(++red_skin_);
        ctx.sound("ses_zaman_red");
        if(red_skin_==5) {
            deadline_=now+number(v(0.5));
            stage_=Stage::charge_wait;
        } else deadline_=now+number(v(1));
        return;
    case Stage::charge_wait:
        if(now<deadline_) return;
        ctx.state.start_turbo=v(0);
        charge_=v(0);
        charge_sample_=0;
        deadline_=now+number(v(0.1));
        stage_=Stage::charge_samples;
        return;
    case Stage::charge_samples:
        if(now<deadline_) return;
        ++charge_sample_;
        if(charge_sample_<5) {
            if(input.key_cuu && charge_==v(0)) charge_=v(20)*v(charge_sample_);
            deadline_=now+number(v(0.1));
            return;
        }
        if(charge_==v(0)) charge_=v(100);
        ctx.sound("ses_zaman_green");
        skin(6);
        ctx.state.ghost_yaris_start=v(1);
        ctx.state.smart_menu_islem=v(0);
        ctx.state.phase=GamePhase::race;
        retract_progress_=v(0);
        retract_done_=false;
        retract_raising_=false;
        stage_=Stage::started;
        return;
    }
    if(stage_==Stage::fade) {
        if(progress_<=v(100)) {
            ctx.state.black_alpha=v(100)-progress_;
            progress_+=v(6.25)/v(1)*time;
            return;
        }
        ctx.state.black_visible=false;
        ctx.state.music_handle=ctx.sound("muzik_oyun_ses",v(75),true);
        deadline_=now+number(v(0.5));
        stage_=Stage::music_wait;
        return;
    }
    if(stage_==Stage::lower) {
        if(progress_<=v(100)) {
            ctx.animate(entity,"indir",progress_);
            progress_+=v(6.25)/v(2)*time;
            return;
        }
        ctx.animate(entity,"indir",v(100));
        deadline_=now+number(v(0.75));
        stage_=Stage::lower_wait;
    }
}

void CountdownPickups::start_boost(GameContext& ctx, const InputSnapshot& input, Var time) {
    if(stage_!=Stage::started) return;
    auto& state=ctx.state;
    if(state.ghost_yaris_start!=v(1) || state.ghost_reset_ok!=v(0) || charge_<=v(0)) {
        state.start_turbo=v(0);
        return;
    }
    if(input.key_cuu) {
        state.start_turbo=v(12)*charge_/v(100);
        if(!boost_played_) {
            ctx.sound("nitro_start_wav",charge_);
            boost_played_=true;
        }
    } else {
        if(boost_played_) charge_=v(0);
        state.start_turbo=v(0);
    }
    charge_-=v(6.25)/v(0.5)*time*(v(1)-state.freeze_mode);
}

void CountdownPickups::rescale_sharks(GameContext& ctx) {
    for(const auto id:sharks_) {
        const Var scale=v(100)+ctx.random(v(160));
        const double value=number(v(7)*scale/v(100));
        ctx.physics.body(id).scale={value,value,value};
    }
}

void CountdownPickups::retract(GameContext& ctx, Var time) {
    if(retract_done_) return;
    if(ctx.state.ghost_reset_ok!=v(0)) {
        rescale_sharks(ctx);
        retract_done_=true;
        return;
    }
    const auto entity=ctx.ids.p_zamanlama;
    if(!retract_raising_) {
        if(retract_progress_<=v(100)) {
            retract_progress_+=v(6.25)/v(0.5)*time;
            return;
        }
        ctx.visuals.at(entity).skin=1;
        ctx.sound("ses_zaman_mekanik2");
        retract_progress_=v(0);
        retract_raising_=true;
    }
    if(retract_progress_<=v(100)) {
        ctx.animate(entity,"kaldir",retract_progress_);
        retract_progress_+=v(6.25)/v(1)*time;
        return;
    }
    ctx.animate(entity,"kaldir",v(100));
    rescale_sharks(ctx);
    retract_done_=true;
}

void CountdownPickups::pickups(GameContext& ctx, const FrameTiming& timing) {
    const auto& car=ctx.physics.body(ctx.ids.p_aracgovde);
    const auto now=timing.seconds_deadlines;
    for(auto& pickup:pickups_) {
        auto& body=ctx.physics.body(pickup.entity);
        auto& visual=ctx.visuals.at(pickup.entity);
        if(pickup.hidden_waits!=v(0)) {
            if(now<pickup.deadline) continue;
            if(pickup.hidden_waits<v(20) && ctx.state.ghost_yaris_start==v(1)) {
                pickup.hidden_waits+=v(1);
                pickup.deadline=now+number(v(0.5));
                continue;
            }
            pickup.hidden_waits=v(0);
            visual.invisible=false;
            // Resume after the inner wait loop, not at the collision test.
        } else if(turbo_pickup_contains(car.position,car.angles,body.position)) {
            visual.invisible=true;
            ctx.sound("turboal_ses_wav");
            ctx.state.turbo_ekle_miktar=physics::var_min(v(300),ctx.state.turbo_ekle_miktar+
                v(25)*v(ctx.world.entities.at(pickup.entity).skills[0]));
            if(ctx.state.ghost_yaris_start==v(1)) {
                pickup.hidden_waits=v(1);
                pickup.deadline=now+number(v(0.5));
                continue;
            }
            visual.invisible=false;
        }
        const auto toward=ctx.state.camera_position-physics::body_source(body.position);
        auto angles=physics::var_vec_to_angle(toward,physics::body_source(body.angles));
        angles.y=angles.z=v(0);
        body.angles=physics::native(angles);
        ctx.animate(pickup.entity,"frame",pickup.frame,true);
        pickup.frame=physics::var_modulo(pickup.frame+v(6.25)/v(1)*timing.originaltime_step_ticks,v(100));
    }
}

void CountdownPickups::update(GameContext& ctx, const InputSnapshot& input,
                             const FrameTiming& timing) {
    countdown(ctx,input,timing);
    start_boost(ctx,input,timing.originaltime_step_ticks);
    retract(ctx,timing.originaltime_step_ticks);
    pickups(ctx,timing);
}
} // namespace c4::game
