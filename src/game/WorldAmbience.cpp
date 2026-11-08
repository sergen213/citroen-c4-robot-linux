#include "WorldAmbience.hpp"

#include <stdexcept>

namespace c4::game {
namespace {
using namespace physics;
constexpr std::array<std::string_view,7> point_actions{
    "SESM_orman1","SESM_orman2","SESM_orman3","SESM_orman4",
    "SESM_deniz1","SESM_deniz2","SESM_deniz3"};
bool suppressed(const SharedState& state) {
    return state.ghost_reset_ok==v(1) || state.freeze_mode>v(0) || state.gameover_panel_acik==v(1);
}
void tune_spatial(GameContext& context, audio::Handle handle, ScriptVec position, Var distance) {
    const auto target_z=property(context.physics.body(context.ids.p_camera_target).position.z);
    const auto volume=source_sound_volume(position,context.state.camera_position,target_z,distance);
    const auto pan=source_sound_pan(position,context.state.camera_position,context.state.camera_angles.x);
    context.audio.tune(handle,static_cast<float>(number(volume)),100,static_cast<float>(number(pan)));
}
}

Var source_sound_pan(ScriptVec source, ScriptVec camera, Var camera_pan) {
    auto balance=var_ang(var_vec_to_angle(source-camera).x-camera_pan);
    if (var_abs(balance)>v(90)) balance=((var_sign(balance)*v(180)-balance)*v(5))/v(9);
    else balance=(balance*v(5))/v(9);
    return -balance;
}

Var source_sound_volume(ScriptVec source, ScriptVec camera, Var target_z, Var maximum_distance) {
    const ScriptVec delta{(source.x-camera.x)/v(1000),
                          (source.y-camera.y)/v(1000),
                          (source.z-camera.z)/v(1000)};
    auto vertical=(v(-3600)-target_z)/v(1000);
    vertical=v(1)-var_clamp(vertical,v(0),v(1));
    const auto distance=var_vec_length(delta)*v(1000);
    return var_max((var_max(maximum_distance-distance,v(0))/maximum_distance)*v(100)*vertical,v(.1));
}

void tune_world_helicopter(GameContext& context, EntityId body) {
    if (context.audio.playing(context.state.helikopter_handle))
        tune_spatial(context,context.state.helikopter_handle,body_source(context.physics.body(body).position),v(15000));
}

WorldAmbience::WorldAmbience(GameContext& context) {
    points_.fill(no_entity);
    for (EntityId id=0;id<context.world.entities.size();++id) {
        for (std::size_t n=0;n<point_actions.size();++n) {
            if (context.world.entities[id].action==point_actions[n]) {
                if (points_[n]!=no_entity) throw std::runtime_error("Duplicate authored world ambience pointer");
                points_[n]=id;
            }
        }
    }
    for (const auto id:points_)
        if (id==no_entity) throw std::runtime_error("Missing authored world ambience pointer");
}

void WorldAmbience::initialize(GameContext&, FrameTiming) {
    initialized_=true;
    stage_=Stage::awaiting_hud;
    processed_=false;
}

void WorldAmbience::stop(GameContext& context) {
    for (auto handle:spatial_handles_) if (context.audio.playing(handle)) context.audio.stop(handle);
    if (context.audio.playing(context.state.ses_sokakhandle)) context.audio.stop(context.state.ses_sokakhandle);
    if (context.audio.playing(context.state.helikopter_handle)) context.audio.stop(context.state.helikopter_handle);
}

void WorldAmbience::teardown(GameContext& context) {
    stop(context);
    initialized_=false;
    stage_=Stage::awaiting_hud;
    processed_=false;
    spatial_handles_.fill(0);
    context.state.ses_sokakhandle=0;
    context.state.helikopter_handle=0;
}

void WorldAmbience::update(GameContext& context, FrameTiming timing) {
    // Global-my HUD survives freeze1; freeze2 skips the complete process queue.
    if (!initialized_ || context.state.freeze_mode==v(2)) return;
    if (processed_ && timing.frame_index<=last_frame_) return;
    processed_=true;
    last_frame_=timing.frame_index;
    if (stage_==Stage::suppressed) {
        if (!suppressed(context.state)) stage_=Stage::running;
        // Source605 exits inner while, skips else607, then yields wait1 at852.
        return;
    }
    // ingame_panels388/507-508: this audio loop is oyunici_panel_ac, not hiz_panel_acil928.
    if (context.state.oyunici_panel_acik!=v(1)) {
        stage_=Stage::awaiting_hud;
        return;
    }
    stage_=Stage::running;
    if (suppressed(context.state)) {
        stop(context);
        stage_=Stage::suppressed;
        return;
    }
    if (!context.audio.playing(context.state.ses_sokakhandle))
        context.state.ses_sokakhandle=context.sound("sokak_ses_ogg",v(20),true);
    if (!context.audio.playing(context.state.helikopter_handle))
        context.state.helikopter_handle=context.sound("helikopter_loop_ogg",v(4),true);
    const auto target_z=property(context.physics.body(context.ids.p_camera_target).position.z);
    for (std::size_t n=0;n<points_.size();++n) {
        const auto position=body_source(context.physics.body(points_[n]).position);
        const auto maximum_distance=n<4?v(8000):v(10000);
        const auto volume=source_sound_volume(position,context.state.camera_position,target_z,maximum_distance);
        auto& handle=spatial_handles_[n];
        if (volume>v(1)) {
            const auto pan=source_sound_pan(position,context.state.camera_position,context.state.camera_angles.x);
            if (!context.audio.playing(handle))
                handle=context.sound(n<4?"orman_ses_wav":"deniz_ses_wav",v(4),true);
            context.audio.tune(handle,static_cast<float>(number(volume)),100,static_cast<float>(number(pan)));
        } else if (context.audio.playing(handle)) context.audio.stop(handle);
    }
    // Positive source wait1 is one later eligible frame, never a time_step countdown.
}
} // namespace c4::game
