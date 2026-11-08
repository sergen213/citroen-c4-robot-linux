#include "GameState.hpp"
#include "RobotController.hpp"
#include "effects/SourceEffects.hpp"
#include "physics/NativeNumeric.hpp"
#include "ui/NativeHud.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace c4;
using physics::number;
using physics::v;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
void wait1_source_boundaries(game::SharedState& state) {
    struct Boundary { std::int32_t freeze; bool entity_ready, null_ready; };
    constexpr std::array<Boundary, 4> boundaries{{
        {1023, true, true}, {1024, false, true},
        {2047, false, true}, {2048, false, false}}};
    for (const auto boundary : boundaries) {
        for (const std::int32_t time_raw : {-1, 0, 1}) {
            // saved MY is not NULL for a live entity, including compact EntityId 0.
            check(game::source_wait1_context_ready(physics::Var{boundary.freeze}, physics::Var{time_raw}, false) ==
                      (time_raw > 0 && boundary.entity_ready),
                  "raw wait(1) entity-context FREEZE/time_frame boundary differs");
            check(game::source_wait1_context_ready(physics::Var{boundary.freeze}, physics::Var{time_raw}, true) ==
                      (time_raw > 0 && boundary.null_ready),
                  "raw wait(1) NULL-context FREEZE/time_frame boundary differs");
        }
    }
    const auto original_freeze = state.freeze_mode;
    const auto next_record_ready = [&](bool saved_my_is_null) {
        return game::source_wait1_context_ready(state.freeze_mode, physics::Var{1}, saved_my_is_null);
    };
    state.freeze_mode = physics::Var{1023};
    check(next_record_ready(false), "live wait(1) record missed sub-one FREEZE");
    state.freeze_mode = physics::Var{1024};
    check(!next_record_ready(false) && next_record_ready(true), "live wait(1) record reused a latched ready value");
    state.freeze_mode = physics::Var{2048};
    check(!next_record_ready(false) && !next_record_ready(true), "live wait(1) record ignored later FREEZE two");
    state.freeze_mode = original_freeze;
}
void same(physics::Vec3 actual, physics::Vec3 expected, std::string_view message) {
    check(actual.x == expected.x && actual.y == expected.y && actual.z == expected.z, message);
}
void same(physics::ScriptVec actual, physics::ScriptVec expected, std::string_view message) {
    check(actual.x.raw == expected.x.raw && actual.y.raw == expected.y.raw &&
          actual.z.raw == expected.z.raw, message);
}
int diagnostic_integer(const game::GameRuntime& runtime, std::string_view key) {
    const auto text = runtime.diagnostics();
    const auto token = "\"" + std::string(key) + "\":";
    const auto offset = text.find(token);
    check(offset != std::string::npos, "missing runtime diagnostic field");
    const char* first = text.data() + offset + token.size();
    int value{};
    const auto parsed = std::from_chars(first, text.data() + text.size(), value);
    check(parsed.ec == std::errc{}, "invalid integer runtime diagnostic field");
    return value;
}
void robot_visible(const game::GameContext& context) {
    for (auto id : {context.ids.p_robot_teker, context.ids.p_robot_mekanik,
                    context.ids.p_robot_farcam, context.ids.p_robot_fargovde,
                    context.ids.p_robot_camlar, context.ids.p_robot_kaporta}) {
        check(!context.visuals.at(id).invisible, "actual robot component is hidden");
        check(!context.group(id).source_vertices.empty(), "robot has no original model vertices");
    }
    check(context.visuals.at(context.ids.p_arac_cam).invisible, "converted car model remains visible");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: c4_game_runtime_tests ORIGINAL_ASSET_ROOT\n";
        return 2;
    }
    try {
        const std::filesystem::path root(argv[1]);
        constexpr std::array<std::string_view, 1> names{"bogazici.wmb"};
        const auto resources = assets::resolve_resources(root / "C4.wrs", names);
        auto world = assets::load_world(resources.at(0));
        check(world.object_count != 0 && !world.entities.empty() && !world.models.empty(),
              "original world contains no real objects/models");
        game::SharedState state;
        wait1_source_boundaries(state);
        std::vector<game::VisualState> visuals;
        const auto prewarm_rng = state.rng_state;
        const auto prewarm_rng_calls = state.rng_calls;
        auto ids = game::bootstrap_game(world, root, state, visuals);
        physics::PhysicsWorld physics(world);
        const auto original_hull = physics.body(ids.p_aracgovde).hull;
        audio::Audio audio(48000, audio::Mode::Offline);
        game::GameRuntime runtime(world, physics, audio, state, ids, visuals, root);
        auto& context = runtime.context();
        ui::Resources hud_resources(root);
        ui::NativeHud hud(hud_resources);
        hud_resources.load();
        std::vector<ui::Quad> hud_quads;
        ui::Painter painter(hud_resources, hud_quads);

        const physics::Vec3 spawn{number(v(27685.279)), number(v(-32348.588)), number(v(-53.770))};
        const physics::Vec3 spawn_angles{number(v(79.985)), number(v(1.228)), number(v(-.038))};
        const auto& initial_body = physics.body(ids.p_aracgovde);
        same(initial_body.position, spawn, "initial body spawn is not the quantized source literal pose");
        same(initial_body.angles, spawn_angles, "initial body angles are not quantized source literals");
        same(physics.body(ids.p_camera_target).position, spawn, "initial source camera target spawn differs");
        check(initial_body.flags.fat && initial_body.flags.narrow && !initial_body.flags.polygon,
              "source set_minmax/POLYGON hull flags were not installed");
        same(initial_body.hull.min, original_hull.min, "source body discarded actual original model minimum bounds");
        same(initial_body.hull.max,
             {original_hull.max.x, original_hull.max.y, number(v(original_hull.max.z) * v(.5))},
             "source body hull did not halve its actual model max_z");
        same(physics.narrow_hull.min, {-90, -45, -36}, "source global narrow hull minimum differs");
        same(physics.narrow_hull.max, {90, 45, 30}, "source global narrow hull maximum differs");
        check(!physics.on_entity, "vehicle event callback started before readiness gate");
        check(physics.size() == world.entities.size() && visuals.size() == world.entities.size(),
              "bootstrap entities were added after physics borrowed the world");
        check(!ids.pickups.empty() && ids.p_aracgovde != ids.p_camera_target, "missing real runtime components");
        std::size_t source_leaves{};
        for (game::EntityId id = 0; id < world.entities.size(); ++id) {
            const auto& entity = world.entities.at(id);
            if (entity.action != "tl1_LeaveInit") continue;
            ++source_leaves;
            check(entity.object_index == game::no_entity && entity.native_prepared_inactive &&
                  visuals.at(id).invisible && !entity.resource.empty() &&
                  entity.model_index < world.models.size(),
                  "source leaf prewarm executed birth or lost its prepared identity");
            check(world.models.at(entity.model_index).sprite.width > 0,
                  "prepared source leaf slot has no actual original sprite");
        }
        check(source_leaves > 0, "source leaf helper slots were not prepared before physics");
        check(state.world_leaf_startup.size() == source_leaves,
              "prepared leaf slots and deferred source callback metadata disagree");
        for (const auto& startup : state.world_leaf_startup) {
            const auto& leaf = world.entities.at(startup.entity);
            check(leaf.action == "tl1_LeaveInit" && leaf.source_position_words.has_value() &&
                  startup.parent < world.entities.size(),
                  "prepared leaf callback lost its exact entity/parent metadata");
            auto prepared = startup.exact_spawn_position;
            prepared.z += startup.height; // Prepared cache only; birth has not executed.
            const auto& words = *leaf.source_position_words;
            check(words[0] == prepared.x.raw && words[1] == prepared.y.raw && words[2] == prepared.z.raw,
                  "prepared leaf cache lost exact source attachment/height words");
        }
        check(state.rng_state == prewarm_rng && state.rng_calls == prewarm_rng_calls,
              "inert bootstrap/menu prewarm consumed source RNG");
        for (std::size_t n = 1; n < ids.path_ent_dizi.size(); ++n) {
            const auto id = ids.path_ent_dizi[n];
            check(id != game::no_entity && !world.entities.at(id).resource.empty(), "missing actual source path entity");
            check(context.group(id).source_vertices.size() >= 4, "source path has no original attachment vertices");
            check(physics.body(id).flags.passable && visuals.at(id).invisible, "source path flags were not applied");
        }
        check(context.source_effects && !context.sounds.empty(), "runtime has no actual effects/audio resources");
        check(context.sounds.size() == 53, "runtime sound cache is not the authored SOUND inventory");
        for (const auto name : {"robot_donus_wav", "muzik_oyun_ses", "ses_zaman_green",
                                "ses_menu_carp", "ses_buton_1", "ses_buton_2", "ses_buton_3",
                                "ses_buton_4", "buton_0_wav", "buton_1_wav", "buton_2_wav",
                                "turboal_ses_wav", "muzik_menu_ses"}) {
            const auto clip = context.clip(name);
            check(clip && clip->sample_rate > 0 && clip->channels > 0 && clip->frames() > 0,
                  "runtime audio binding has no decoded original PCM");
            const std::string_view identity(name);
            const bool loop = identity == "muzik_menu_ses";
            const auto volume = v(loop ? 70 : identity == "ses_buton_1" ? 60 :
                identity == "buton_0_wav" || identity == "buton_1_wav" ||
                identity == "buton_2_wav" ? 50 : 100);
            const auto first = context.sound(name, volume, loop);
            const auto second = context.sound(name, volume, loop);
            check(first != 0 && second != 0 && first != second &&
                  audio.playing(first) && audio.playing(second),
                  "shared original PCM did not create independent playback handles");
            check(audio.state(first).volume == number(volume) && audio.state(first).loop == loop,
                  "original UI sound volume/loop intent differs");
            audio.stop(first);
            check(audio.playing(second), "stopping one original sound stopped its repeated voice");
            audio.stop(second);
        }
        check(context.clip("ses_buton_1") == context.clip("ses_checkpoint"),
              "authored hover/checkpoint aliases did not share decoded original PCM");

        std::uint64_t frame{};
        double wall_offset{};
        game::InputSnapshot latest_input{};
        game::FrameTiming latest_timing{};
        const auto step = [&](game::InputSnapshot input = {},
                              game::UiCommand continuation = game::UiCommand::none) {
            check(frame < 5000, "runtime acceptance exceeded its finite 5000-frame bound");
            const game::FrameTiming timing{v(.5), ++frame, static_cast<double>(frame) / 32 + wall_offset};
            latest_input = input;
            latest_timing = timing;
            runtime.advance_source_processes(input, timing);
            if (continuation != game::UiCommand::none)
                runtime.dispatch_ui_command(continuation, input, timing);
            runtime.finish_source_frame(input, timing);
            check(audio.rendered_frames() == frame * 1500,
                  "runtime did not advance its real offline mixer once per source tick");
            if (state.oyunici_panel_acik != v(1))
                check(state.hud_vites_frame == v(0) && state.hud_motor_frame == v(0),
                      "source motor/HUD frames advanced before the actual HUD gate");
            hud.update(state, timing);
            runtime.publish_draw_frame(); // Sole draw publication after the initial UI pass.
            hud_quads.clear();
            hud.draw(painter);
        };
        check(state.phase == game::GamePhase::menu && state.ghost_reset_ok == v(1), "runtime did not begin at source menu/reset");
        step({}, game::UiCommand::play);
        check(state.phase == game::GamePhase::intro, "Play skipped actual source intro");
        const auto intro_camera = state.camera_angles;
        bool orbit_changed{}, reset_seen{};
        std::array<bool, 7> countdown_skins{};
        physics::Body reset_boundary;
        while (state.phase != game::GamePhase::race) {
            const auto previous_reset = state.ghost_reset_ok;
            step({.key_cuu = true});
            if (state.phase == game::GamePhase::intro) {
                orbit_changed |= state.camera_angles.x != intro_camera.x;
                check(state.oyun_oynanabilir <= v(100) && !physics.on_entity,
                      "source vehicle process launched during intro/loading waits");
            }
            if (state.phase == game::GamePhase::countdown) {
                check(state.oyun_oynanabilir == v(100), "intro did not open source readiness100 gate");
                check(static_cast<bool>(physics.on_entity) == (state.ghost_reset_ok == v(0)),
                      "source vehicle callback did not follow its countdown reset boundary");
                const auto skin = visuals.at(ids.p_zamanlama).skin;
                check(skin >= 1 && skin <= 6, "countdown selected invalid actual light skin");
                countdown_skins.at(static_cast<std::size_t>(skin)) = true;
                check(state.ghost_yaris_start == v(0) && state.arac_ivme == v(0),
                      "held Up accelerated before source green/start gate");
            }
            if (previous_reset == v(1) && state.ghost_reset_ok == v(0)) {
                check(!reset_seen, "initial countdown reset boundary repeated");
                check(state.oyun_oynanabilir == v(100) && physics.on_entity,
                      "source reset did not launch the ready vehicle process");
                reset_seen = true;
                reset_boundary = physics.body(ids.p_aracgovde);
                check(reset_boundary.position.x == spawn.x && reset_boundary.position.y == spawn.y,
                      "initial reset did not place actual body at quantized source spawn XY");
                check(state.turbo_ekle_miktar == v(0), "initial source reset retained turbo bonus");
            }
        }
        check(orbit_changed && reset_seen && state.oyun_oynanabilir == v(100), "actual intro orbit/reset/readiness path was not exercised");
        for (int skin = 2; skin <= 5; ++skin)
            check(countdown_skins.at(static_cast<std::size_t>(skin)), "actual source red-light stage was skipped");
        check(visuals.at(ids.p_zamanlama).skin == 6 && state.ghost_yaris_start == v(1), "race began without actual source green light");
        check(state.oyunici_panel_acik == v(1), "actual source HUD never opened");
        check(state.history_epoch > 1, "actual intro/chase/reset never invalidated native history");

        const auto before_drive = physics.body(ids.p_aracgovde).position;
        const auto driving_history = state.history_epoch;
        for (int n = 0; n < 48; ++n) step({.key_cuu = true});
        check(state.history_epoch == driving_history, "ordinary moving source poses invalidated native history");
        check(state.arac_ivme > v(0), "held Up produced no actual source acceleration");
        const auto driven = physics.body(ids.p_aracgovde).position - before_drive;
        check(std::hypot(driven.x, driven.y) > 0, "held Up produced no actual horizontal body movement");
        check(!hud_quads.empty() && hud.needle_roll() < 50 && std::isfinite(hud.needle_roll()),
              "actual source HUD did not compute/draw a moving speed gauge");
        const auto speed = static_cast<int>(number(physics::var_abs(state.arac_ivme) * v(.8)));
        check(speed > 0 && diagnostic_integer(runtime, "hud_speed") == speed, "published source HUD speed differs from computed acceleration");

        const auto before_camera_mode_history = state.history_epoch;
        step({.key_c = true});
        const auto user_camera_mode = state.camera_tipno;
        check(user_camera_mode == v(1), "actual source C input did not change user camera mode");
        check(state.history_epoch > before_camera_mode_history, "actual C camera-mode transition retained native history");
        step(); // Release C and D through the real controller before activating D.
        check(state.history_epoch == before_camera_mode_history + 1, "ordinary C release invalidated native history");
        step({.key_d = true});
        using RobotPhase = game::RobotController::Phase;
        check(state.arac_durum_tip == v(1) && state.model_robot_ok == v(1), "released-D source gate did not activate robot");
        check(diagnostic_integer(runtime, "robot_phase") == static_cast<int>(RobotPhase::conversion), "D skipped actual multi-frame robot conversion");
        robot_visible(context);
        const auto first_robot_frame = state.robot_frame_no;
        int conversion_frames{};
        bool conversion_advanced{}, left_conversion{};
        for (int n = 0; n < 128; ++n) {
            step(); // Immediately release D: conversion is a persistent source process.
            check(state.arac_durum_tip == v(1) && state.model_robot_ok == v(1), "released D discarded persistent robot mode");
            robot_visible(context);
            const auto phase = diagnostic_integer(runtime, "robot_phase");
            if (phase == static_cast<int>(RobotPhase::conversion)) ++conversion_frames;
            else left_conversion = true;
            conversion_advanced |= state.robot_frame_no != first_robot_frame;
        }
        check(conversion_frames > 1 && conversion_advanced && left_conversion,
              "robot conversion did not persist/progress across actual runtime frames");
        step({.key_d = true});
        const auto reverse_phase = diagnostic_integer(runtime, "robot_phase");
        check(state.arac_durum_tip == v(1) && state.model_robot_ok == v(1) &&
              reverse_phase >= static_cast<int>(RobotPhase::reverse_blend) &&
              reverse_phase <= static_cast<int>(RobotPhase::reverse_inis),
              "held D toggled directly back to car instead of entering multi-frame reverse");
        robot_visible(context);

        step({.key_esc = true, .key_esc_pressed = true});
        check(state.phase == game::GamePhase::paused && state.freeze_mode == v(1), "Esc did not open source pause menu");
        const auto paused_body = physics.body(ids.p_aracgovde);
        const auto paused_target = physics.body(ids.p_camera_target);
        const auto paused_camera = state.camera_position;
        const auto paused_camera_angles = state.camera_angles;
        const auto paused_camera_arc = state.camera_arc;
        const auto paused_camera_near = state.camera_clip_near;
        const auto paused_camera_far = state.camera_clip_far;
        const auto paused_acceleration = state.arac_ivme;
        const auto paused_clock = state.t_oyun_zaman;
        const auto paused_rng = state.rng_state;
        const auto paused_rng_calls = state.rng_calls;
        wall_offset += 3600; // Wall time must not become a source simulation catch-up.
        for (int n = 0; n < 24; ++n) {
            step({.key_cuu = true, .key_d = true, .key_c = true,
                  .key_esc = n != 0, .key_esc_pressed = n == 1});
            check(state.phase == game::GamePhase::paused, "repeated Esc toggled pause into resume");
            same(physics.body(ids.p_aracgovde).position, paused_body.position, "paused actual body position changed");
            same(physics.body(ids.p_aracgovde).angles, paused_body.angles, "paused actual body angles changed");
            same(physics.body(ids.p_camera_target).position, paused_target.position, "paused actual target changed");
            same(state.camera_position, paused_camera, "paused camera position changed");
            same(state.camera_angles, paused_camera_angles, "paused camera angles changed");
            check(state.camera_arc.raw == paused_camera_arc.raw &&
                  state.camera_clip_near.raw == paused_camera_near.raw &&
                  state.camera_clip_far.raw == paused_camera_far.raw,
                  "paused source VIEW scalar properties changed");
            check(state.arac_ivme == paused_acceleration && state.t_oyun_zaman == paused_clock,
                  "paused source acceleration/race clock changed while HUD updated");
            check(state.rng_state == paused_rng && state.rng_calls == paused_rng_calls,
                  "paused native RNG state advanced while HUD updated");
            check(!hud_quads.empty(), "actual HUD stopped drawing during pause");
        }
        const auto captured_view = runtime.capture_draw_view();
        const auto before_ui2_audio = audio.rendered_frames();
        const auto before_ui2_effects = context.source_effects->frame().counts;
        const auto before_ui2_effect_revision = context.source_effects->frame().revision;
        runtime.dispatch_ui_command(game::UiCommand::resume, latest_input, latest_timing);
        const auto after_ui2_view = runtime.capture_draw_view();
        same(after_ui2_view.position, captured_view.position, "UI2 Resume changed captured raw VIEW position");
        same(after_ui2_view.angles, captured_view.angles, "UI2 Resume changed captured raw VIEW angles");
        check(after_ui2_view.arc.raw == captured_view.arc.raw &&
              after_ui2_view.clip_near.raw == captured_view.clip_near.raw &&
              after_ui2_view.clip_far.raw == captured_view.clip_far.raw &&
              after_ui2_view.effective_near == captured_view.effective_near &&
              after_ui2_view.pixel_aspect == captured_view.pixel_aspect,
              "UI2 Resume changed captured VIEW scalar properties");
        const auto after_ui2_effects = context.source_effects->frame().counts;
        check(after_ui2_effects.active == before_ui2_effects.active &&
              after_ui2_effects.emitted == before_ui2_effects.emitted &&
              after_ui2_effects.expired == before_ui2_effects.expired &&
              after_ui2_effects.allocation_failures == before_ui2_effects.allocation_failures &&
              after_ui2_effects.recycled == before_ui2_effects.recycled &&
              context.source_effects->frame().revision == before_ui2_effect_revision,
              "UI2 Resume reran particles or republished the effect frame");
        check(audio.rendered_frames() == before_ui2_audio, "UI2 Resume advanced the offline mixer");
        check(state.phase == game::GamePhase::race && state.freeze_mode == v(0), "UI Resume failed");
        check(state.t_oyun_zaman == paused_clock && state.arac_ivme == paused_acceleration,
              "late Resume callback advanced source clock/acceleration in its callback frame");
        same(physics.body(ids.p_aracgovde).position, paused_body.position,
             "late Resume callback advanced actual body before the next source pass");
        same(physics.body(ids.p_aracgovde).angles, paused_body.angles,
             "late Resume callback advanced actual body angles before the next source pass");
        same(state.camera_position, paused_camera, "late Resume callback advanced raw camera position");
        same(state.camera_angles, paused_camera_angles, "late Resume callback advanced raw camera angles");
        check(state.rng_state == paused_rng && state.rng_calls == paused_rng_calls,
              "late Resume callback advanced source RNG before the next source pass");
        step();
        check(state.t_oyun_zaman > paused_clock && state.t_oyun_zaman - paused_clock <= v(1),
              "Resume caught up wall time instead of advancing one source half-tick");

        step({.key_esc = true, .key_esc_pressed = true});
        check(state.phase == game::GamePhase::paused, "second source pause failed");
        const auto tuning = state.fe_yol_tutusmik;
        const auto old_music = state.music_handle;
        check(audio.playing(old_music), "race/countdown did not leave its owned music loop active");
        runtime.dispatch_ui_command(game::UiCommand::restart, latest_input, latest_timing);
        check(!audio.playing(old_music) && state.music_handle == 0,
              "resumed Restart left its old owned music loop active");
        const auto restart_frame = frame;
        const auto before_restart_history = state.history_epoch;
        check(state.ghost_reset_ok == v(1), "UI Restart skipped its source reset/wait stage");
        while (state.ghost_reset_ok != v(0)) step();
        check(frame == restart_frame + 5, "Restart did not preserve one wait plus five countdown wait stages");
        check(state.history_epoch > before_restart_history, "actual source restart retained native history");
        const auto& restarted = physics.body(ids.p_aracgovde);
        // This is the observable reset boundary AFTER the mandatory first real
        // controller tick, not a claim that native physics matches engine motion.
        same(restarted.position, reset_boundary.position, "Restart did not reproduce actual source body reset boundary pose");
        same(restarted.angles, reset_boundary.angles, "Restart did not reproduce actual source body reset boundary angles");
        check(restarted.position.x == spawn.x && restarted.position.y == spawn.y,
              "Restart used global zero instead of quantized source spawn XY");
        check(state.turbo_ekle_miktar == v(0) && state.start_turbo == v(0), "Restart retained source turbo charge");
        check(state.arac_durum_tip == v(0) && state.model_robot_ok == v(0), "Restart retained robot mode");
        check(state.oyun_oynanabilir == v(100) && state.fe_yol_tutusmik == tuning && state.arac_max_hiz >= v(350),
              "Restart replaced source reset with all-state zeroing");
        check(state.camera_tipno == user_camera_mode || state.camrec_regno == user_camera_mode,
              "Restart discarded the user's source camera mode");
        step();
        check(state.camera_tipno == user_camera_mode, "Restart failed to restore actual user camera mode");
        while (state.phase != game::GamePhase::race) step();
        check(state.ghost_yaris_start == v(1) && state.ghost_reset_ok == v(0), "Restart never returned through actual countdown into race");
        check(!audio.playing(old_music) && audio.playing(state.music_handle),
              "restarted countdown overlapped the old owned music loop");

        step({.key_esc = true, .key_esc_pressed = true});
        runtime.dispatch_ui_command(game::UiCommand::menu, latest_input, latest_timing);
        check(state.phase == game::GamePhase::menu && state.level1_yukleme_ok == v(1),
              "repeat-load regression did not return from the loaded race to Main Menu");
        // The actual NativeUi menu-return callback publishes these four source VIEW
        // flags before its menu wait (game_over.wdl:1705-1715).
        state.noshader = state.bloom_noshader = true;
        state.noparticle = state.bloom_noparticle = true;
        state.bloom_fx_ok = v(0);
        const auto reload_camera = state.camera_position;
        runtime.dispatch_ui_command(game::UiCommand::play, latest_input, latest_timing);
        const auto reload_frame = frame;
        const auto gates_closed = [&] {
            return state.noshader && state.bloom_noshader &&
                   state.noparticle && state.bloom_noparticle;
        };
        check(gates_closed() && state.loading_visible && state.loading_stage == 4 &&
              state.loading_progress == v(0), "repeat Play skipped the source reload-bar entry wait");
        same(state.camera_position, reload_camera, "repeat Play incorrectly started the first-load camera orbit");
        for (int n = 1; n <= 80; ++n) {
            step();
            check(gates_closed() && state.loading_visible &&
                  state.loading_progress == v(1.25) * v(n),
                  "repeat-load bar changed VIEW gates or lost the <=100 source wait boundary");
            same(state.camera_position, reload_camera, "repeat-load bar ran the first-load camera orbit");
        }
        step(); // ana_menu.wdl:3029-3030: full bar, fps_max=32, wait(1).
        check(gates_closed() && state.loading_visible && state.loading_progress == v(101.25),
              "repeat-load full-bar wait cleared source VIEW gates early");
        const auto global_street_handle = state.ses_sokakhandle;
        const auto before_reload_voices = audio.active_voices();
        step(); // ana_menu.wdl:3032-3039: street loop, hide panels, wait(1).
        check(gates_closed() && !state.loading_visible &&
              state.ses_sokakhandle == global_street_handle &&
              audio.active_voices() == before_reload_voices + 1,
              "repeat-load street loop overwrote the HUD ambience handle or lost its independent voice");
        step(); // ana_menu.wdl:3040-3045: four clears, bloom_acil, c4_yaris_bitir.
        const auto reloaded_view = runtime.capture_draw_view();
        check(frame == reload_frame + 83 && !state.noshader && !state.bloom_noshader &&
              !state.noparticle && !state.bloom_noparticle &&
              !reloaded_view.no_shader && !reloaded_view.no_particles &&
              state.bloom_fx_ok == v(1) && state.gamma_corrected_active &&
              state.phase == game::GamePhase::countdown && state.ghost_reset_ok == v(1),
              "repeat-load tail did not clear both source VIEW gates at the resumed source phase");
        check(state.ses_sokakhandle == global_street_handle &&
              audio.active_voices() == before_reload_voices + 1,
              "source ambience suppression stopped the reload function's independent local loop");
        for (int n = 0; n < 4; ++n) {
            step();
            check(state.ghost_reset_ok == v(1), "repeat-load skipped c4_yaris_bitir/countdown waits");
        }
        step();
        check(frame == reload_frame + 88 && state.ghost_reset_ok == v(0),
              "repeat-load reset differs from its source wait/countdown continuation");
        while (state.phase != game::GamePhase::race) step();
        step({.key_esc = true, .key_esc_pressed = true});
        runtime.dispatch_ui_command(game::UiCommand::restart, latest_input, latest_timing);
        check(!state.noshader && !state.bloom_noshader,
              "Restart after repeat Play disabled restored source effects");
        while (state.ghost_reset_ok != v(0)) step();
        check(!runtime.capture_draw_view().no_shader && !state.noshader && !state.bloom_noshader,
              "Restart continuation lost repeat-load source effects admission");
        check(audio.rendered_frames() == frame * 1500,
              "runtime did not exercise finite actual original-clip offline mixing");
        std::cout << runtime.diagnostics() << '\n';
        std::cout << "real original-world runtime orchestration passed: source 32Hz half-ticks, "
                     "actual HUD/components/reset; original SDK RNG algorithm in native call scope only, "
                     "engine motion/scheduler/RNG call-order parity not proven\n";
    } catch (const std::exception& error) {
        std::cerr << "c4_game_runtime_tests: " << error.what() << '\n';
        return 1;
    }
}
