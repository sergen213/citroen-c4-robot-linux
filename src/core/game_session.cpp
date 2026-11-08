#include "game_session.hpp"

#include "assets/Resources.hpp"
#include "game/CameraProjection.hpp"
#include "game/InputScenario.hpp"
#include "game/effects/SourceEffects.hpp"
#include "game_render_bridge.hpp"
#include "rendering/SourceLighting.hpp"

#include <array>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace c4 {
namespace {
std::filesystem::path original_app_root(const Settings& settings) {
    if (settings.game_dir.empty())
        throw std::runtime_error("Native gameplay requires imported original assets: pass --game-dir with the imported root or its app directory.");
    auto root = std::filesystem::canonical(settings.game_dir);
    if (!std::filesystem::is_directory(root))
        throw std::runtime_error("Native gameplay asset root is not a directory: " + root.string());
    if (!std::filesystem::is_regular_file(root / "kaynak_kap.rc4") &&
        !std::filesystem::is_regular_file(root / "bogazici.wmb") &&
        std::filesystem::is_directory(root / "app"))
        root = std::filesystem::canonical(root / "app");
    return root;
}

rendering::RenderScene prepare_scene(const Settings& settings, const std::filesystem::path& root,
                                    game::SharedState& state, std::vector<game::VisualState>& visuals,
                                    game::ComponentIds& ids,
                                    const assets::GpuImageRuntimeContext& file_context) {
    auto prepared = settings;
    if (prepared.world.empty()) {
        const auto loose = root / "bogazici.wmb";
        const auto archive = root / "kaynak_kap.rc4";
        if (std::filesystem::is_regular_file(loose)) prepared.world = loose;
        else if (std::filesystem::is_regular_file(archive)) {
            constexpr std::array<std::string_view, 1> names{"bogazici.wmb"};
            prepared.world = assets::resolve_resources(archive, names).at(0);
        } else {
            throw std::runtime_error("Native gameplay requires original bogazici.wmb or kaynak_kap.rc4 in " + root.string());
        }
    }
    auto world = assets::load_world(prepared.world, effective_source_profile(settings));
    if (world.entities.empty() || world.models.empty())
        throw std::runtime_error("Native gameplay requires the original bogazici world with its real entities and models: " + prepared.world.string());
    ids = game::bootstrap_game(world, root, state, visuals);
    // The original engine clamps the source clip_near=0 to an effective 1.
    state.camera_effective_near = 1;
    state.camera_pixel_aspect = 1;
    auto scene = rendering::load_render_scene(prepared, std::move(world), file_context);
    rendering::prepare_source_sky(scene, prepared.world);
    // Actual normal resource100 load: SHADOW_MODE=0xf000 lifecycle is known.
    // Floor output is invariant here: trace OFF skips; ON misses in both kernels.
    // This does not establish final POLY/TRACE controls or default mode74;
    // constructor ENV=127/chroma=0 remains distinct from raw no-hit seed400.
    const rendering::SourceFloorTraceControls controls{0xf000, 1024, 1024, true, true};
    rendering::initialize_source_entity_lighting_caches(scene.entity_lighting_caches,
        rendering::source_scene_no_floor_hits(*scene.world, controls),
        rendering::source_scene_no_linked_brightness_writes(*scene.world));
    return scene;
}

game::GameContext& initialize_ui(ui::NativeUi& ui, game::GameRuntime& runtime) {
    auto& context = runtime.context();
    const game::FrameTiming timing{physics::v(0), 0, 0, physics::v(0)};
    ui.advance_continuations(context, timing);
    ui.update(context, timing);
    return context;
}

audio::Audio initialize_audio(std::string& device_error) {
    try {
        return audio::Audio(48000, audio::Mode::Device);
    } catch (const audio::AudioDeviceError& error) {
        device_error = error.what();
        std::cerr << "Warning: native audio device unavailable: " << device_error
                  << "; using the offline mixer (no audible output).\n";
        return audio::Audio(48000, audio::Mode::Offline);
    }
}

void json_string(std::ostream& out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') out << '\\' << static_cast<char>(ch);
        else if (ch < 32) out << "\\u00" << hex[ch >> 4] << hex[ch & 15];
        else out << static_cast<char>(ch);
    }
    out << '"';
}
} // namespace

struct GameSession::Impl {
    std::filesystem::path root;
    game::SharedState state;
    std::vector<game::VisualState> actor_visuals;
    game::ComponentIds ids;
    rendering::RenderScene scene;
    physics::PhysicsWorld physics;
    std::string audio_device_error;
    audio::Audio audio;
    game::GameRuntime runtime;
    const game::SourceEffects& effects;
    ui::NativeUi ui;
    rendering::GameRenderBridge bridge;
    rendering::NativeGameVisualState globals;
    rendering::SourceLightingState lighting;
    rendering::OriginalBloomState bloom;
    rendering::OriginalGammaState gamma;
    std::vector<std::uint8_t> gamma_bytes;
    std::optional<game::InputScenario> scenario;
    game::InputSnapshot scenario_input;
    std::uint64_t source_frames{};
    int gamma_brightness{};
    game::SourceViewSnapshot draw_view;
    rendering::CameraState draw_camera;
    bool draw_world{};
    bool selected_bloom_view{};
    int pixel_width{}, pixel_height{};

    Impl(const Settings& settings, const assets::GpuImageRuntimeContext& file_context)
        : root(original_app_root(settings)),
          scene(prepare_scene(settings, root, state, actor_visuals, ids, file_context)),
          physics(*scene.world), audio(initialize_audio(audio_device_error)),
          runtime(*scene.world, physics, audio, state, ids, actor_visuals, root),
          effects(*runtime.context().source_effects),
          ui(root, file_context, settings.width, settings.height), bridge(scene, initialize_ui(ui, runtime)),
          pixel_width(settings.width), pixel_height(settings.height) {
        if (!settings.input_script.empty()) scenario.emplace(game::InputScenario::load(settings.input_script));
        // new_color.wdl selects the LUT from the actual source compile
        // selector, independently of native quality and runtime bloom_aktif.
        gamma_bytes = assets::read_resource(root /
            (scene.world->source_profile.bloom ? "rca1.rc4" : "rca0.rc4"));
        gamma_brightness = static_cast<int>(physics::number(state.GA_Parlaklik));
        gamma = rendering::decode_original_gamma(gamma_bytes, gamma_brightness);
        capture_world(false);
        refresh_gamma();
    }

    void dispatch_ui_command(game::UiCommand command, const game::InputSnapshot& input,
                             const game::FrameTiming& timing) {
        if (command == game::UiCommand::select_local) ui.set_selected_ghost(1);
        else if (command == game::UiCommand::select_web) ui.set_selected_ghost(2);
        runtime.dispatch_ui_command(command, input, timing);
    }

    void capture_world(bool source_draw) {
        draw_view = runtime.capture_draw_view();
        scene.source_effects_enabled = !draw_view.no_shader;
        // Same proven VIEW selection gate as capture_source_view, not a pose test.
        const bool next_bloom_view = bool(state.bloom_aktif) && bool(state.bloom_fx_ok);
        if (selected_bloom_view != next_bloom_view) ++scene.history_epoch;
        selected_bloom_view = next_bloom_view;
        const bool next_draw_world = ui.frame().draw_world;
        if (draw_world != next_draw_world) ++scene.history_epoch;
        draw_world = next_draw_world; // Actual blank/world admission transition.
        bridge.synchronize();
        globals = rendering::source_game_visual_state(*scene.world,
            physics::native(draw_view.position), physics::native(draw_view.angles));
        globals.source_view_flags = draw_view.flags_value;
        globals.source_view_flags_known_mask = draw_view.flags_known_mask;
        lighting = rendering::source_lighting_state(*scene.world, globals);
        rendering::publish_source_view_ambient(lighting,
            draw_view.effective_ambient, draw_view.effective_ambient_known);
        bloom.enabled = state.bloom_aktif != physics::v(0) && state.bloom_fx_ok != physics::v(0);
        draw_camera = game::source_camera(draw_view, pixel_width, pixel_height);
        if (source_draw && draw_world) {
            // Only accepted world VIEW entries dispatch callbacks. Prewarm
            // and the original blank menu/loading world have no such entry.
            // flags contains only the consumed, proven CULL_CW mask.
            const rendering::SourceSpriteView sprite_view{
                static_cast<float>(physics::number(draw_view.angles.x)),
                static_cast<float>(physics::number(draw_view.angles.y)),
                draw_camera.projection[0], draw_camera.projection[5], draw_view.flags_value & 0x20000u};
            bridge.synchronize_view(globals, sprite_view);
            rendering::capture_original_root_alpha(scene);
        }
    }

    void refresh_gamma() {
        const auto brightness = static_cast<int>(physics::number(state.GA_Parlaklik));
        if (brightness != gamma_brightness) {
            gamma_brightness = brightness;
            gamma = rendering::decode_original_gamma(gamma_bytes, brightness);
        }
        gamma.corrected_active = state.gamma_corrected_active;
    }
};

GameSession::GameSession(const Settings& settings, const assets::GpuImageRuntimeContext& file_context)
    : impl_(std::make_unique<Impl>(settings, file_context)) {}
GameSession::~GameSession() = default;
rendering::RenderScene& GameSession::scene() { return impl_->scene; }
ui::NativeUi& GameSession::ui() { return impl_->ui; }
physics::Var GameSession::source_mouse_mode() const { return impl_->state.mouse_mode; }
void GameSession::set_viewport(int width, int height, bool) {
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("Native gameplay needs a positive pixel viewport");
    // The modern Hor+ camera retains the observed 640x480 VIEW.aspect=1
    // baseline; applying the legacy fullscreen correction would distort it.
    impl_->state.camera_pixel_aspect = 1;
    if (impl_->pixel_width != width || impl_->pixel_height != height) {
        impl_->pixel_width = width;
        impl_->pixel_height = height;
        impl_->draw_camera = game::source_camera(impl_->draw_view, width, height);
    }
}
void GameSession::update(game::InputSnapshot input, const ui::MenuInput& menu, double seconds_deadlines) {
    auto& session = *impl_;
    const game::FrameTiming timing{physics::v(.5), session.source_frames, seconds_deadlines, physics::v(.5)};
    if (session.scenario) {
        if (session.source_frames <= session.scenario->last_frame())
            session.scenario_input = session.scenario->frame(session.source_frames);
        else {
            session.scenario_input.ui_command = game::UiCommand::none;
            session.scenario_input.mouse_force_x = {};
            session.scenario_input.key_esc_pressed = false;
            session.scenario_input.key_f1_pressed = false;
        }
        input = session.scenario_input;
    }
    auto pass_input = menu;
    pass_input.pointer_enabled = session.state.mouse_mode != physics::v(0);
    auto scripted_command = input.ui_command;
    input.ui_command = game::UiCommand::none;
    if (session.ui.captures_game_input(pass_input)) {
        input = {};
        scripted_command = game::UiCommand::none;
    }

    session.runtime.advance_source_processes(input, timing);
    session.bridge.advance_material_processes(timing.originaltime_step_ticks,
        timing.originaltime_frame_ticks);
    auto& context = session.runtime.context();
    session.dispatch_ui_command(session.ui.advance_continuations(context, timing), input, timing);
    // These scenario commands name resumed source-script bodies, not the
    // physical menu callbacks that start their fades and waits.
    switch (scripted_command) {
    case game::UiCommand::play:
    case game::UiCommand::restart:
    case game::UiCommand::menu:
    case game::UiCommand::quit:
        session.dispatch_ui_command(scripted_command, input, timing);
        scripted_command = game::UiCommand::none;
        break;
    default:
        break;
    }
    session.runtime.finish_source_frame(input, timing);
    session.ui.update(context, timing);
    session.bridge.reconcile_material_events(timing.originaltime_step_ticks);

    pass_input.pointer_enabled = session.state.mouse_mode != physics::v(0);
    const auto before_world = session.ui.input(pass_input, ui::UiPass::BeforeWorld);
    if (session.ui.captures_game_input()) scripted_command = game::UiCommand::none;
    session.dispatch_ui_command(scripted_command == game::UiCommand::none ? before_world : scripted_command,
                                input, timing);
    session.ui.apply_callbacks(context);
    session.ui.compose(session.state);
    session.runtime.publish_draw_frame();
    session.capture_world(true);

    pass_input.pointer_enabled = session.state.mouse_mode != physics::v(0);
    session.dispatch_ui_command(session.ui.input(pass_input, ui::UiPass::AfterWorld), input, timing);
    session.ui.apply_callbacks(context);
    session.ui.compose(session.state);
    session.refresh_gamma();
    ++session.source_frames;
}
rendering::FrameSnapshot GameSession::frame(int width, int height) const {
    const auto& session = *impl_;
    const auto& overlay = session.ui.frame();
    const auto instances = session.draw_world
        ? std::span<const rendering::Instance>(session.scene.instances)
        : std::span<const rendering::Instance>{};
    const auto camera = width == session.pixel_width && height == session.pixel_height
        ? session.draw_camera : game::source_camera(session.draw_view, width, height);
    rendering::FrameSnapshot result{session.scene, instances, camera};
    result.overlay = &overlay;
    result.visuals = &session.globals;
    result.bloom = &session.bloom;
    result.effects = session.draw_world ? &session.effects.frame() : nullptr;
    result.gamma = &session.gamma;
    result.source_lighting = &session.lighting;
    result.source_effects_enabled = !session.draw_view.no_shader;
    result.history_epoch = session.scene.history_epoch;
    if (session.scene.sky)
        result.sky = game::source_sky_frame(*session.scene.sky, session.draw_view, camera, session.draw_world);
    return result;
}
bool GameSession::finished() const { return impl_->state.quit_requested; }
std::uint64_t GameSession::source_frames() const { return impl_->source_frames; }
bool GameSession::input_scenario_finished() const {
    return impl_->scenario && impl_->scenario->finished(impl_->source_frames);
}
std::string GameSession::diagnostics() const {
    auto game = impl_->runtime.diagnostics();
    if (game.empty() || game.back() != '}')
        throw std::logic_error("Native gameplay diagnostics is not a JSON object");
    game.pop_back();
    std::ostringstream out;
    out << game << ",\"source_callback_frames\":" << impl_->source_frames
        << ",\"ui_images\":" << impl_->ui.frame().images.size()
        << ",\"ui_quads\":" << impl_->ui.frame().quads.size()
        << ",\"gamma_corrected_active\":" << (impl_->gamma.corrected_active ? "true" : "false")
        << ",\"gamma_brightness\":" << impl_->gamma_brightness
        << ",\"source_bloom_enabled\":" << (impl_->bloom.enabled ? "true" : "false")
        << ",\"original_ui_root\":";
    json_string(out, impl_->root.string());
    out << ",\"audio_device_error\":";
    json_string(out, impl_->audio_device_error);
    out << ",\"audio_device_open\":" << (impl_->audio.mode() == audio::Mode::Device ? "true" : "false");
    const auto& profile = impl_->scene.world->source_profile;
    out << ",\"source_compile_options\":{\"ATQ\":" << static_cast<unsigned>(profile.atq)
        << ",\"MTQ\":" << static_cast<unsigned>(profile.mtq)
        << ",\"STQ\":" << static_cast<unsigned>(profile.stq)
        << ",\"CGM\":" << static_cast<unsigned>(profile.cgm)
        << ",\"SHQ\":" << static_cast<unsigned>(profile.shq)
        << ",\"ANO\":" << static_cast<unsigned>(profile.ano)
        << ",\"Bloom\":" << static_cast<unsigned>(profile.bloom) << '}';
    const auto& session = *impl_;
    const auto& state = session.state;
    auto raw_vector = [&](std::string_view name, const physics::ScriptVec& value) {
        json_string(out, name);
        out << ":[" << value.x.raw << ',' << value.y.raw << ',' << value.z.raw << ']';
    };
    auto raw_body = [&](std::string_view name, game::EntityId id) {
        out << ',';
        json_string(out, name);
        const bool present = id != game::no_entity && id < session.physics.size();
        out << ":{\"entity_id\":" << id << ",\"body_present\":" << (present ? "true" : "false");
        if (present) {
            const auto& body = std::as_const(session.physics).body(id);
            out << ',';
            raw_vector("position_raw", physics::body_source(body.position));
            out << ',';
            raw_vector("angles_raw", physics::body_source(body.angles));
        } else {
            out << ",\"position_raw\":null,\"angles_raw\":null";
        }
        out << '}';
    };
    auto raw_live_view = [&](std::string_view name, const physics::ScriptVec& position,
                             const physics::ScriptVec& angles, physics::Var arc,
                             physics::Var near, physics::Var far, physics::Var ambient) {
        out << ',';
        json_string(out, name);
        out << ":{";
        raw_vector("position_raw", position);
        out << ',';
        raw_vector("angles_raw", angles);
        out << ",\"arc_raw\":" << arc.raw << ",\"clip_near_raw\":" << near.raw
            << ",\"clip_far_raw\":" << far.raw << ",\"ambient_raw\":" << ambient.raw << '}';
    };
    // Requested diagnostics only: these observed fields are not a full continuation snapshot.
    // Existing source_raw/source_vector_raw/source_array_raw remain companion comparison fields.
    out << ",\"source_state\":{\"schema_version\":1"
        << ",\"coverage\":\"observed_player_camera_phase_rng_world_queue\""
        << ",\"equivalence_scope\":\"not_complete_hidden_controller_ui_particle_actor_local_state\""
        << ",\"completed_source_frames\":" << session.source_frames
        << ",\"last_source_frame_index\":";
    if (session.source_frames) out << session.source_frames - 1;
    else out << "null";
    out << ",\"phase\":" << static_cast<std::uint32_t>(state.phase)
        << ",\"phase_request\":" << static_cast<std::uint32_t>(state.phase_request)
        << ",\"freeze_raw\":" << state.freeze_mode.raw
        << ",\"rng_state\":" << state.rng_state << ",\"rng_calls\":" << state.rng_calls;
    raw_body("player", session.ids.p_aracgovde);
    raw_body("camera_target", session.ids.p_camera_target);
    raw_live_view("live_primary_view", state.camera_position, state.camera_angles, state.camera_arc,
                  state.camera_clip_near, state.camera_clip_far, state.camera_ambient);
    // Match current native secondary VIEW selection, not a direct original Bloom near observation.
    raw_live_view("live_bloom_view", state.bloom_camera_position, state.bloom_camera_angles,
                  state.bloom_camera_arc, physics::Var{10463}, state.camera_clip_far,
                  state.bloom_camera_ambient);
    const auto& draw = session.draw_view; // Retained before UI2; do not recapture the live view.
    out << ",\"retained_draw_view\":{";
    raw_vector("position_raw", draw.position);
    out << ',';
    raw_vector("angles_raw", draw.angles);
    out << ",\"arc_raw\":" << draw.arc.raw << ",\"clip_near_raw\":" << draw.clip_near.raw
        << ",\"clip_far_raw\":" << draw.clip_far.raw
        << ",\"no_shader\":" << (draw.no_shader ? "true" : "false")
        << ",\"no_particle\":" << (draw.no_particles ? "true" : "false")
        << ",\"flags_value\":" << draw.flags_value << ",\"flags_known_mask\":" << draw.flags_known_mask
        << ",\"effective_ambient\":" << draw.effective_ambient
        << ",\"effective_ambient_known\":" << (draw.effective_ambient_known ? "true" : "false") << '}';
    const auto& sky = session.scene.sky;
    out << ",\"source_sky\":{\"prepared\":" << (sky ? "true" : "false") << ",\"resource\":";
    if (sky) json_string(out, sky->resource);
    else out << "null";
    out << ",\"width\":" << (sky ? sky->first_face_width : 0)
        << ",\"height\":" << (sky ? sky->first_face_height : 0)
        << ",\"stage_known\":" << (sky && sky->stage_enabled_known ? "true" : "false")
        << ",\"stage_enabled\":" << (sky && sky->stage_enabled ? "true" : "false")
        << ",\"parsed_empty_root_known\":"
        << (session.scene.world->source_empty_collision_root_known ? "true" : "false")
        << ",\"draw_world\":" << (session.draw_world ? "true" : "false")
        << ",\"projection_known\":" << (session.draw_camera.projection_known ? "true" : "false")
        << ",\"frame_eligible\":"
        << (sky && game::source_sky_frame(*sky, draw, session.draw_camera, session.draw_world)
            ? "true" : "false") << '}';
    out << ",\"source_compile_profile\":{\"ATQ\":" << static_cast<unsigned>(profile.atq)
        << ",\"MTQ\":" << static_cast<unsigned>(profile.mtq)
        << ",\"STQ\":" << static_cast<unsigned>(profile.stq)
        << ",\"CGM\":" << static_cast<unsigned>(profile.cgm)
        << ",\"SHQ\":" << static_cast<unsigned>(profile.shq)
        << ",\"ANO\":" << static_cast<unsigned>(profile.ano)
        << ",\"Bloom\":" << static_cast<unsigned>(profile.bloom) << '}';
    const auto queue_size = session.runtime.source_world_queue_size();
    out << ",\"world_queue_count\":" << queue_size << ",\"ordered_world_queue\":[";
    for (std::size_t ordinal = 0; ordinal < queue_size; ++ordinal) {
        if (ordinal) out << ',';
        out << session.runtime.source_world_queued_entity(ordinal);
    }
    out << "]}";
    out << '}';
    return out.str();
}
audio::Audio& GameSession::audio() { return impl_->audio; }
bool GameSession::cursor_visible() const { return impl_->ui.cursor_visible(); }

} // namespace c4
