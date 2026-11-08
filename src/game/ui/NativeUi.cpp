#include "NativeUi.hpp"
#include "GraphicsPanel.hpp"
#include "HudMeshes.hpp"
#include "MenuPanel.hpp"
#include "MenuVideo.hpp"
#include "NativeHud.hpp"
#include "UiResources.hpp"
#include "game/GameState.hpp"
#include "physics/NativeNumeric.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace c4::ui {
using physics::number;
using physics::v;

struct NativeUi::Impl {
    explicit Impl(const std::filesystem::path& root, const assets::GpuImageRuntimeContext& file_context,
                  int launch_width, int launch_height)
        : resources(root, file_context, launch_width, launch_height), menu(resources), hud(resources),
          hud_meshes(resources), graphics(resources), video(root / "video.wmv") {
        cursor = resources.add(std::array{"arrow_640.tga", "arrow_800.tga",
                                          "arrow_1024.tga", "arrow_1280.tga"}[resources.tier()]);
        resources.load();
        menu.set_hud_source_panels(hud.source_panels());
        black_curtain = resources.find("LY_Fade.bmp");
        const auto& curtain = resources.image(black_curtain);
        root_curtain_pick_ = {v(0), v(0), v(resources.canvas_width() / 30),
                              v(resources.canvas_height() / 7), curtain.width, curtain.height,
                              99, source_ui_order(0, 201), false};
        menu.set_root_source_curtain(root_curtain_pick_);
        hud_meshes.initialize();
        video_slot = resources.find("menu_videoekran.tga");
        resources.mark_dynamic(video_slot);
        if (resources.image(video_slot).width != video.width() ||
            resources.image(video_slot).height != video.height())
            throw std::runtime_error("Original video surface and WMV dimensions disagree");
        quads.reserve(8192);
        refresh_frame();
    }
    void refresh_frame() noexcept {
        frame = {resources.images, quads, static_cast<float>(resources.canvas_width()),
                 static_cast<float>(resources.canvas_height()), resources.revisions,
                 hud_meshes.meshes(), !menu.menu_visible() && !menu.loading_visible(),
                 hud_meshes.prepared_meshes(), resources.gpu_images};
    }
    void sound(game::GameContext& context) {
        for (auto source = menu.take_sound(); !source.empty(); source = menu.take_sound()) {
            const auto volume = source == "ses_buton_1" ? v(60)
                : source == "buton_0_wav" || source == "buton_1_wav" || source == "buton_2_wav"
                    ? v(50) : v(100);
            context.sound(source, volume);
        }
    }
    Resources resources;
    MenuPanel menu;
    NativeHud hud;
    HudMeshes hud_meshes;
    GraphicsPanel graphics;
    MenuVideo video;
    std::vector<Quad> quads;
    UiFrame frame;
    MenuInput pointer;
    SourcePanelPickState root_curtain_pick_;
    std::uint32_t cursor{}, black_curtain{}, video_slot{};
    bool menu_open{}, video_started{}, video_stopped{}, cursor_shown{}, brightness_changed{}, graphics_input_captured{};
    double video_start{};
};

NativeUi::NativeUi(const std::filesystem::path& root, const assets::GpuImageRuntimeContext& file_context,
                   int launch_width, int launch_height)
    : impl_(std::make_unique<Impl>(root, file_context, launch_width, launch_height)) {}
NativeUi::~NativeUi() = default;
game::UiCommand NativeUi::input(const MenuInput& input, UiPass pass) {
    auto& ui = *impl_;
    ui.pointer = input;
    if (pass == UiPass::BeforeWorld)
        ui.graphics_input_captured = ui.graphics.is_open();
    if (ui.graphics.is_open() || ui.graphics_input_captured) {
        (void)ui.menu.observe_pointer(input); // Capture blocks callbacks, not source button-held sampling.
        if (pass == UiPass::BeforeWorld)
            ui.graphics.input(input);
        ui.menu.finish_ui_pass(pass);
        return game::UiCommand::none;
    }
    auto source_input = input;
    if (pass == UiPass::AfterWorld) {
        source_input.tab = source_input.enter = source_input.left = source_input.right = false;
        source_input.escape_pressed = source_input.escape_released = false;
    }
    const auto brightness = ui.menu.settings().brightness;
    const auto command = ui.menu.input(source_input);
    ui.brightness_changed = ui.brightness_changed || brightness != ui.menu.settings().brightness;
    if (ui.menu.take_graphics_open_request())
        ui.graphics.open();
    ui.menu.finish_ui_pass(pass);
    return command;
}
void NativeUi::apply_callbacks(game::GameContext& context) {
    auto& ui = *impl_;
    // Original arrow cues run before master_vol is changed by the callback.
    ui.sound(context);
    if (const auto mode = ui.menu.take_mouse_mode_write())
        context.state.mouse_mode = *mode;
    const auto& settings = ui.menu.settings();
    context.state.GA_Volume = v(settings.volume);
    context.state.GA_Parlaklik = v(settings.brightness);
    if (ui.brightness_changed) {
        context.state.gamma_corrected_active = false; // Source set_gamma_ramp(gr_default,...).
        ui.brightness_changed = false;
    }
    context.state.GA_MusicOk = v(settings.music);
    context.state.c4_texture_rengi = v(settings.color);
    context.state.selected_ghost = v(settings.ghost);
    context.audio.set_master_volume(static_cast<float>(settings.volume));
    if (ui.menu.take_video_stop_request())
        ui.video_stopped = true;
    if (ui.menu.take_music_stop_request())
        context.stop_music();
    if (ui.menu.take_results_hud_restore())
        ui.hud.restore_source_panels(context.state);
    if (ui.menu.take_menu_return_view_flags()) {
        context.audio.stop(context.state.ses_sokakhandle);
        context.state.ses_sokakhandle = 0;
        context.stop_music();
        context.state.noparticle = true;
        context.state.noshader = true;
        context.state.bloom_noparticle = true;
        context.state.bloom_noshader = true;
    }
}
game::UiCommand NativeUi::advance_continuations(game::GameContext& context, game::FrameTiming timing) {
    auto& ui = *impl_;
    ui.hud.sync_source_visibility(context.state);
    if (ui.menu.starts_menu_video(timing)) {
        ui.video.restart();
        ui.menu.start_laser(context.random(v(800)));
        ui.video_started = true;
        ui.video_start = timing.seconds_deadlines;
        ui.video_stopped = false;
    }
    ui.menu.update(context.state, timing);
    apply_callbacks(context);
    return impl_->menu.take_continuation_command();
}
void NativeUi::update(game::GameContext& context, game::FrameTiming timing) {
    auto& ui = *impl_;
    apply_callbacks(context);
    ui.menu.synchronize(context.state, timing);
    apply_callbacks(context);
    ui.hud.update(context.state, timing);
    context.state.gauge_needle_roll = ui.hud.source_needle_roll();
    ui.hud_meshes.update(context.state, ui.hud.source_visibility());

    const bool menu_open = ui.menu.menu_visible();
    if (menu_open && !ui.menu_open) {
        ui.video_started = false;
        context.state.music_handle = context.sound("muzik_menu_ses", v(70), true);
    }
    ui.menu_open = menu_open;
    if (ui.menu.menu_art_visible() && ui.video_started && !ui.video_stopped) {
        if (ui.video.update(timing.seconds_deadlines - ui.video_start,
                            ui.resources.images[ui.video_slot]))
            ++ui.resources.revisions[ui.video_slot];
    }

    compose(context.state);
}
void NativeUi::compose(const game::SharedState& state) {
    auto& ui = *impl_;
    const bool menu_open = ui.menu.menu_visible();
    const bool loading = ui.menu.loading_visible();
    const bool menu_curtain = ui.menu.owns_source_curtain();
    const bool results_backdrop = state.game_over_ok == v(1) && state.gameover_panel_acik == v(1);
    const bool loading_curtain = !menu_curtain && !results_backdrop && loading && state.black_visible;
    const bool other_curtain = !menu_curtain && !results_backdrop && !menu_open && !loading &&
        state.phase != game::GamePhase::results && state.black_visible;
    ui.root_curtain_pick_.visible = loading_curtain || other_curtain;
    ui.quads.clear();
    Painter painter(ui.resources, ui.quads);
    ui.hud.draw(painter);
    // Source curtain layer99 is below the loading artwork (layer100+).
    if (loading_curtain) {
        painter.set_layer(99);
        painter.set_source_order(source_ui_order(0, 201));
        painter.image(ui.black_curtain, 0, 0, static_cast<float>(number(ui.root_curtain_pick_.sx)),
                      static_cast<float>(number(ui.root_curtain_pick_.sy)),
                      static_cast<float>(number(state.black_alpha) / 100.0), false);
    }
    ui.menu.draw(painter);
    if (other_curtain) {
        painter.set_layer(99);
        painter.set_source_order(source_ui_order(0, 201));
        painter.image(ui.black_curtain, 0, 0, static_cast<float>(number(ui.root_curtain_pick_.sx)),
                      static_cast<float>(number(ui.root_curtain_pick_.sy)),
                      static_cast<float>(number(state.black_alpha) / 100.0), false);
    }
    painter.set_source_order(0); // Authored graphics extension has no original declaration rank.
    ui.graphics.draw(painter);
    ui.cursor_shown = state.mouse_mode == v(2) || ui.graphics.is_open();
    if (ui.cursor_shown && ui.pointer.pointer_inside) {
        painter.set_layer(1001); // Engine mouse map follows the authored graphics extension, too.
        painter.image(ui.cursor, ui.pointer.x, ui.pointer.y, 1, 1);
    }
    // ponytail: stable insertion allocates nothing; bucket by layer if source
    // content grows enough to make the currently small HUD/result inversions costly.
    const auto before = [](const Quad& a, const Quad& b) noexcept {
        return a.layer < b.layer || (a.layer == b.layer && a.source_order > b.source_order);
    };
    for (auto current = ui.quads.begin(); current != ui.quads.end(); ++current) {
        if (current == ui.quads.begin() || !before(*current, *(current - 1)))
            continue;
        const auto insertion = std::upper_bound(ui.quads.begin(), current, *current, before);
        std::rotate(insertion, current, current + 1);
    }
    ui.refresh_frame();
}
const UiFrame& NativeUi::frame() const noexcept { return impl_->frame; }
const UiSettings& NativeUi::settings() const noexcept { return impl_->menu.settings(); }
void NativeUi::set_selected_ghost(int choice) { impl_->menu.set_selected_ghost(choice); }
void NativeUi::configure_graphics(const Settings& active,
    const rendering::quality::GraphicsCapabilities& capabilities,
    const rendering::quality::ResolvedGraphics& effective,
    std::uint32_t supported_rt_effects, std::uint32_t active_rt_effects,
    std::string_view backend_name, std::string_view capability_reason,
    std::string_view apply_error, std::optional<bool> observed_vsync) {
    impl_->graphics.configure(active, capabilities, effective, supported_rt_effects,
                              active_rt_effects, backend_name, capability_reason, apply_error, observed_vsync);
}
std::optional<Settings> NativeUi::take_graphics_request() { return impl_->graphics.take_request(); }
bool NativeUi::cursor_visible() const noexcept { return impl_->cursor_shown; }
bool NativeUi::captures_game_input() const noexcept { return impl_->graphics.is_open(); }
bool NativeUi::captures_game_input(const MenuInput& input) const noexcept {
    return impl_->graphics.is_open() || impl_->menu.would_open_graphics(input);
}
double NativeUi::needle_roll() const noexcept { return impl_->hud.needle_roll(); }
double NativeUi::gauge_frame() const noexcept { return impl_->hud.gauge_frame(); }
} // namespace c4::ui
