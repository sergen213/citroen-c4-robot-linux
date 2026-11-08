#pragma once

#include "UiResources.hpp"
#include "game/GameState.hpp"

#include <array>
#include <optional>
#include <string>
#include <span>
#include <string_view>

namespace c4::ui {

class MenuPanel {
public:
    enum class Screen { main, color, ghost, settings, credits, scores, exit, hidden,
                        loading, pause, results, help, confirm_restart, confirm_menu };
    explicit MenuPanel(Resources&);
    void update(const game::SharedState&, game::FrameTiming);
    void synchronize(const game::SharedState&, game::FrameTiming);
    game::UiCommand take_continuation_command() noexcept;
    void draw(Painter&);
    game::UiCommand input(const MenuInput&);
    // Sample the engine history without dispatching buttons (native Graphics capture).
    int observe_pointer(const MenuInput&) noexcept;
    bool would_open_graphics(const MenuInput&) const noexcept;
    void finish_ui_pass(UiPass) noexcept;
    void set_hud_source_panels(std::span<const SourcePanelPickState>);
    void set_root_source_curtain(const SourcePanelPickState& panel) noexcept {
        root_source_curtain_ = &panel;
    }
    const UiSettings& settings() const noexcept { return settings_; }
    // Drain until empty after each input/update; source press and hover are distinct callbacks.
    std::string_view take_sound() noexcept;
    Screen screen() const noexcept { return screen_; }
    bool ready() const noexcept;
    bool menu_visible() const noexcept;
    bool menu_art_visible() const noexcept { return menu_art_visible_; }
    bool owns_source_curtain() const noexcept;
    bool starts_menu_video(game::FrameTiming timing) const noexcept {
        return transition_ == Transition::startup_wait && deadline_ != 0
            && timing.seconds_deadlines >= deadline_;
    }
    bool cursor_visible() const noexcept;
    // Source callback writes only; drain before and after update without phase-derived defaults.
    std::optional<physics::Var> take_mouse_mode_write() noexcept;
    void start_laser(physics::Var phase) noexcept { laser_time_ = phase; }
    bool loading_visible() const noexcept { return screen_ == Screen::loading; }
    bool take_graphics_open_request() noexcept;
    bool take_video_stop_request() noexcept;
    bool take_music_stop_request() noexcept;
    bool take_menu_return_view_flags() noexcept;
    bool take_results_hud_restore() noexcept;
    void set_selected_ghost(int preference);
private:
    enum class Transition { startup_wait, fade, shutter_wait, shutter, entrance_wait,
                            outgoing_nudge, outgoing, gap, incoming, bounce1,
                            bounce2, bounce3, bounce4, idle, command_fade, command_wait,
                            quit_wait, quit_black_wait, menu_return_wait, result_restart_wait,
                            loading_initial_wait, loading_media_wait, loading_hide_wait,
                            loading_purge_wait, loading_save_wait };
    enum class Action { none, color, settings, scores, credits, exit, back, next, color_left,
                        color_right, volume_down, volume_up, brightness_down,
                        brightness_up, server, local, no_ghost, start, quit, resume, restart,
                        menu, yes, no, graphics };
    struct Button {
        std::uint32_t idle{}, hover{}, pressed{};
        float x{}, y{};
        Action action{};
        float native_width{}, native_height{};
    };
    enum class PanelId : std::size_t {
        menu_black, menu_bottom, menu_right, tv, video, tile1, tile2,
        arrow1a, arrow1b, arrow2a, arrow2b, front, shutter, top, car, laser, robot1, robot2,
        main_inert, main_active, color_inert, color_active, ghost_inert, ghost_active,
        settings_inert, settings_active, credits_inert, credits_active, scores_inert, scores_active,
        exit_inert, exit_active, loading_bg, smoke1, smoke2, loading_bar,
        result_bg, result_base, result_buttons, result_restart, result_menu,
        pause_bg, pause_base, pause_buttons, pause_restart, pause_menu, help, curtain,
        hud0, hud1, hud2, hud3, hud4, hud5, count
    };
    static constexpr std::size_t panel_count = static_cast<std::size_t>(PanelId::count);
    static constexpr std::size_t panel_index(PanelId id) noexcept { return static_cast<std::size_t>(id); }
    enum class HoverCallback { none, menu, gameplay, server, local, no_ghost };
    struct NativeButton {
        std::array<std::uint32_t,3> images{};
        int x{}, y{}, keyboard_index{-1};
        Action action{Action::none};
        HoverCallback hover{HoverCallback::none};
    };
    struct NativePanel {
        std::array<NativeButton,10> buttons{};
        std::size_t button_count{};
        physics::Var width{}, height{};
        int layer{};
        std::uint32_t source_order{};
        Screen screen{Screen::hidden}, return_screen{Screen::pause};
        bool active{};
    };
    struct PanelGeometry {
        physics::Var x{}, y{}, sx{}, sy{}, width{}, height{};
        int layer{};
        std::uint32_t source_order{};
        bool visible{};
    };
    struct PointerTarget { int panel{-1}, button{-1}; };
    struct PointerDispatch {
        game::UiCommand command{game::UiCommand::none};
        bool consumed{}, first_callback{};
    };
    void initialize_source_panels();
    PanelGeometry source_panel(std::size_t) const noexcept;
    int select_source_panel(const MenuInput&) const noexcept;
    int hit_native_button(std::size_t, const MenuInput&) const noexcept;
    PointerTarget source_pointer_target(const MenuInput&) const noexcept;
    PointerDispatch dispatch_source_panel(std::size_t, const MenuInput&, int);
    std::uint32_t source_button_image(std::size_t, std::size_t, std::size_t) const noexcept;
    int hit_graphics(const MenuInput&) const noexcept;
    void hover_callback(HoverCallback);
    void transition(Screen, game::UiCommand = game::UiCommand::none);
    void advance(physics::Var ticks, double seconds);
    void build_buttons(Screen, physics::Var x);
    std::size_t active_panel(Screen) const noexcept;
    void draw_screen(Painter&, Screen, physics::Var x, bool active);
    void draw_menu_art(Painter&);
    game::UiCommand activate(Action, Screen origin, Screen return_screen);
    void persist_settings();
    void reset_main(bool first);
    void cue(std::string_view);
    physics::Var canvas_x(double value) const noexcept { return physics::v(value) * physics::v(resources_.scale_x()); }
    physics::Var canvas_y(double value) const noexcept { return physics::v(value) * physics::v(resources_.scale_y()); }
    void text(Painter&, std::string_view, float x, float y, bool centered = false) const;
    void draw_panel_image(Painter&, PanelId, std::uint32_t image, float alpha = 1, bool filtered = true) const;
    Resources& resources_;
    UiSettings settings_;
    Screen screen_{Screen::main}, target_{Screen::main}, confirmation_return_{Screen::pause};
    Transition transition_{Transition::startup_wait};
    game::GamePhase phase_{game::GamePhase::menu};
    game::UiCommand pending_{game::UiCommand::none}, command_{game::UiCommand::none};
    std::array<Button, 6> buttons_{};
    std::array<std::array<std::uint32_t,3>,21> button_images_{};
    // Original PANEL identities include inert panes, passive NULL buttons, and the six live HUD panes.
    std::array<NativePanel,panel_count> native_panels_{};
    std::array<std::array<int,10>,panel_count> button_transients_{};
    std::array<std::array<int,10>,panel_count> rendered_button_transients_{};
    std::array<bool,panel_count> previously_drawn_{};
    std::span<const SourcePanelPickState> hud_source_panels_;
    int current_panel_{-1}, previous_panel_{-1};
    const SourcePanelPickState* root_source_curtain_{};
    int graphics_transient_{}, rendered_graphics_transient_{};
    bool source_bounds_initialized_{}, ghost_server_selected_{};
    Screen buttons_screen_{Screen::hidden};
    std::size_t button_count_{};
    int focus_{-1}, hover_{-1}, ghost_info_{};
    bool first_{true}, incoming_visible_{}, result_buttons_{}, bloom_{};
    bool menu_art_visible_{true}, loading_bar_visible_{};
    int loading_wait_frames_{};
    bool result_backdrop_visible_{};
    bool graphics_open_request_{};
    bool video_stop_request_{};
    bool music_stop_request_{};
    bool menu_return_view_flags_{};
    bool results_hud_restore_{};
    bool previous_ui_down_{};
    std::optional<physics::Var> mouse_mode_write_;
    physics::Var result_backdrop_alpha_{};
    std::array<physics::Var,7> inert_x_{};
    physics::Var fade_{physics::Var::literal(100)};
    physics::Var fade_progress_{physics::Var::literal(100)};
    physics::Var current_ticks_{};
    physics::Var shutter_y_{}, tile_y_{}, arrow_y_{}, laser_time_{}, laser_alpha_{};
    physics::Var tile1_y_{}, arrow1_y_{};
    physics::Var smoke1_x_{};
    physics::Var pointer_cursor_x_{}, pointer_cursor_y_{};
    physics::Var credits_offset_{}, smoke_x_{}, loading_width_{physics::Var::literal(1)}, timer_{};
    double seconds_{}, deadline_{};
    std::array<std::string_view,4> sounds_{};
    std::size_t sound_begin_{}, sound_count_{};
    std::string result_text_, persistence_error_, record_error_;
    std::array<std::uint32_t,3> server_images_{};
    std::uint32_t time_font_{}, white_{}, fade_image_{}, ghost_blank_image_{};
    std::array<std::uint32_t, 10> cars_{}, robots1_{}, robots2_{}, icons_{}, color_names_{};
    std::array<std::uint32_t, 11> volumes_{}, brightness_{}, dark_{};
    std::array<std::uint32_t, 4> ghost_info_images_{};
};
} // namespace c4::ui
