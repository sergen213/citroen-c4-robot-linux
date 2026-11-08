#include "WorldAmbience.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace c4;
using physics::v;
void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: c4_world_ambience_tests ORIGINAL_ASSET_ROOT\n";
        return 2;
    }
    try {
        const physics::ScriptVec origin{};
        check(game::source_sound_pan({v(1000),v(0),v(0)},origin,v(0)) == v(0), "front balance");
        check(game::source_sound_pan({v(0),v(1000),v(0)},origin,v(0)) == v(-50), "left balance");
        check(game::source_sound_pan({v(0),v(-1000),v(0)},origin,v(0)) == v(50), "right balance");
        check(game::source_sound_pan({v(-1000),v(1000),v(0)},origin,v(0)) == v(-25), "rear bearing fold");
        check(game::source_sound_pan({v(1000),v(0),v(0)},origin,v(270)) == v(-50), "normalized camera bearing");
        check(game::source_sound_volume(origin,origin,v(0),v(8000)) == v(100), "near volume");
        check(game::source_sound_volume(origin,origin,v(-4100),v(8000)) == v(50), "vertical attenuation");
        check(game::source_sound_volume(origin,origin,v(-4600),v(8000)) == v(.1), "vertical volume floor");
        check(game::source_sound_volume({v(8000),v(0),v(0)},origin,v(0),v(8000)) == v(.1), "range volume floor");
        // Divide each Q10 coordinate BEFORE vec_length; 0.5 source units vanish here.
        check(game::source_sound_volume({v(.5),v(0),v(0)},origin,v(0),v(8000)) == v(100), "normalized-distance operation order");

        const std::filesystem::path root(argv[1]);
        constexpr std::array<std::string_view,1> names{"bogazici.wmb"};
        auto world = assets::load_world(assets::resolve_resources(root/"C4.wrs",names).at(0));
        game::SharedState state;
        std::vector<game::VisualState> visuals;
        auto ids = game::bootstrap_game(world,root,state,visuals);
        physics::PhysicsWorld physics(world);
        audio::Audio audio(48000,audio::Mode::Offline);
        game::GameRuntime runtime(world,physics,audio,state,ids,visuals,root);
        auto& ctx = runtime.context();
        for (const auto name : {"helikopter_loop_ogg","sokak_ses_ogg","orman_ses_wav","deniz_ses_wav"}) {
            const auto clip = ctx.clip(name);
            check(clip->frames()>0 && clip->sample_rate>0, "original ambience PCM is not decoded");
        }
        audio.stop_all();
        std::size_t points{};
        game::EntityId helicopter = game::no_entity;
        for (game::EntityId id=0;id<world.entities.size();++id) {
            const auto& action=world.entities[id].action;
            if (action=="GMA_heli_govde") helicopter=id;
            if (action=="SESM_orman1" || action=="SESM_orman2" || action=="SESM_orman3" || action=="SESM_orman4" ||
                action=="SESM_deniz1" || action=="SESM_deniz2" || action=="SESM_deniz3") {
                check(physics.body(id).flags.passable && visuals[id].invisible, "finite source audio pointer flags");
                physics.body(id).position={};
                ++points;
            }
        }
        check(points==7 && helicopter!=game::no_entity,"missing actual world ambience entities");
        state.camera_position={};state.camera_angles={};
        physics.body(ids.p_camera_target).position={};
        physics.body(helicopter).position={};
        game::WorldAmbience ambience(ctx);
        ambience.initialize(ctx,{});
        std::uint64_t frame{};
        const auto tick=[&] { ambience.update(ctx,{v(.5),++frame,static_cast<double>(frame)/32}); };
        state.ghost_reset_ok=v(0);
        tick();
        check(audio.active_voices()==0,"world preload incorrectly launched HUD sounds");
        state.oyunici_panel_acik=v(1);
        tick();
        check(audio.active_voices()==9,"HUD did not start nine real ambience handles");
        check(audio.state(state.helikopter_handle).volume==4,"helicopter HUD start volume");
        game::tune_world_helicopter(ctx,helicopter);
        check(audio.state(state.helikopter_handle).volume==100,"helicopter actor did not tune actual HUD handle");
        const auto first_handle=state.helikopter_handle;
        check(audio.state(first_handle).frame==0 && audio.rendered_frames()==0,"source process advanced offline audio clock");
        std::array<float,3000> pcm{};
        audio.render(pcm);
        check(audio.rendered_frames()==1500 && audio.state(first_handle).frame>0,"real offline mixer did not advance decoded loop");
        check(audio.active_voices()==9,"decoded loops unexpectedly expired");
        state.freeze_mode=v(2);tick();
        check(audio.active_voices()==9,"freeze2 did not skip the HUD process queue");
        state.freeze_mode=v(1);tick();
        check(audio.active_voices()==0 && state.helikopter_handle==first_handle,"freeze suppression did not stop and retain source handles");
        state.freeze_mode=v(0);tick();
        check(audio.active_voices()==0,"suppression clear skipped source trailing wait1");
        ambience.update(ctx,{v(4),frame,99999});
        check(audio.active_voices()==0,"same frame resumed positive wait1 from time_step or deadline");
        tick();check(audio.active_voices()==9,"suppression resume did not restart loops");
        state.ghost_reset_ok=v(1);tick();check(audio.active_voices()==0,"restart did not stop loops");
        state.ghost_reset_ok=v(0);tick();check(audio.active_voices()==0,"restart clear consumed no source wait1");
        tick();check(audio.active_voices()==9,"restart discarded HUD continuation");
        state.gameover_panel_acik=v(1);tick();check(audio.active_voices()==0,"gameover did not stop loops");
        state.gameover_panel_acik=v(0);tick();tick();
        check(audio.active_voices()==9,"gameover suppression did not resume");
        physics.body(ids.p_camera_target).position.z=physics::number(v(-4600));
        tick();check(audio.active_voices()==2,"inaudible forest/ocean loops were not stopped");
        ambience.teardown(ctx);check(audio.active_voices()==0,"world unload left ambience alive");
        check(audio.rendered_frames()==1500,"source continuation consumed offline audio without global mixer");
        std::cout << "original world ambience source boundaries checked\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
