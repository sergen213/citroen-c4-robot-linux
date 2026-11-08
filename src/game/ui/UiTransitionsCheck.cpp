#include "MenuPanel.hpp"
#include "GraphicsPanel.hpp"
#include "SourceViewSprite.hpp"
#include "NativeHud.hpp"

// Keep this behavior executable effective in Release builds, too.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "c4-ui-transitions-XXXXXX").string();
        const char* created = ::mkdtemp(pattern.data());
        if (!created) throw std::system_error(errno, std::generic_category(), "mkdtemp");
        path = created;
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};
struct Environment {
    const char* name;
    std::optional<std::string> previous;
    Environment(const char* key, const std::filesystem::path& value) : name(key) {
        if (const char* old = std::getenv(name)) previous = old;
        if (::setenv(name, value.c_str(), 1) != 0)
            throw std::system_error(errno, std::generic_category(), "setenv");
    }
    ~Environment() {
        if (previous) ::setenv(name, previous->c_str(), 1);
        else ::unsetenv(name);
    }
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
};
void check_graphics_requests(c4::ui::GraphicsPanel& panel, c4::ui::Resources& resources) {
    namespace quality = c4::rendering::quality;
    c4::Settings active;
    active.game_dir = resources.root;
    active.frames = 73;
    active.diagnostics = true;
    active.vsync = true;
    const quality::GraphicsCapabilities caps{};
    const auto configure = [&](const c4::Settings& settings) {
        panel.configure(settings, caps, quality::resolve_graphics(settings.graphics, caps),
                        0, 0, settings.renderer, "UI behavior fixture; not a GPU capability observation.", "", std::nullopt);
    };
    const auto click = [&](float x, float y) {
        c4::ui::MenuInput input;
        input.x = x*resources.scale_x()/.625f;
        input.y = y*resources.scale_y()/.625f; input.pointer_inside = true;
        input.pointer_pressed = true;
        panel.input(input);
        input.pointer_pressed = false; input.pointer_released = true;
        panel.input(input);
    };
    const auto preserved = [&](const c4::Settings& request) {
        assert(request.game_dir == active.game_dir);
        assert(request.frames == active.frames && request.diagnostics == active.diagnostics);
        assert(request.vsync == active.vsync);
    };

    configure(active);
    panel.open();
    click(350, 68); // Display tab: requested VSync is on, actual state is unknown.
    std::vector<c4::ui::Quad> drawn, expected;
    c4::ui::Painter painter(resources, drawn), expected_painter(resources, expected);
    panel.draw(painter);
    constexpr std::array<int,4> widths{5,7,9,11}, heights{10,14,19,23};
    expected_painter.bitmap_text(resources.find("time_beyazfont_" + std::to_string(resources.tier()) + ".tga"),
                                 widths[resources.tier()],heights[resources.tier()],
                                 "unknown",425*resources.scale_x()/.625f,150*resources.scale_y()/.625f);
    std::size_t glyph = 0;
    for (const auto& quad : drawn) {
        if (quad.image_index != expected.front().image_index ||
            quad.rect[1] != 150*resources.scale_y()/.625f ||
            quad.rect[0] < 425*resources.scale_x()/.625f) continue;
        assert(glyph < expected.size());
        assert(quad.image_index == expected[glyph].image_index);
        assert(quad.rect == expected[glyph].rect && quad.uv == expected[glyph].uv);
        ++glyph;
    }
    assert(glyph == expected.size() && !expected.empty());
    click(100, 68); // Quality changes must retain requested VSync.
    click(400, 110); // Original -> Enhanced.
    click(400, 110); // Enhanced -> Ultra, despite current OpenGL RT support being zero.
    click(100, 428);
    const auto ultra = panel.take_request();
    assert(ultra && ultra->renderer == "vulkan" && ultra->rt_effects == 7);
    assert(ultra->graphics.preset == quality::Preset::Ultra);
    assert(quality::requested_profile(ultra->graphics) == quality::Preset::Ultra);
    preserved(*ultra);
    assert(!panel.take_request());
    click(100, 428); // Consuming the request does not clear the pending transaction.
    assert(!panel.take_request());

    configure(active);
    panel.open();
    click(400, 110);
    click(400, 110);
    click(200, 68); // Ray effects tab.
    click(400, 110); // Disable reflections, retaining the Ultra quality base.
    click(100, 428);
    const auto custom = panel.take_request();
    assert(custom && custom->renderer == "vulkan" && custom->rt_effects == 6);
    assert(custom->graphics.preset == quality::Preset::Ultra);
    assert(quality::requested_profile(custom->graphics) == quality::Preset::Custom);
    preserved(*custom);
    assert(!panel.take_request());

    configure(*custom); // Same-backend fixture now authoritatively reports no supported RT.
    panel.open();
    click(100, 428);
    assert(!panel.take_request() && panel.is_open());
    panel.input({.escape_pressed = true});
    assert(!panel.is_open() && !panel.take_request());
}
void check_source_view_sprite() {
    const auto vertices = c4::ui::original_view_sprite_vertices(156, 25);
    const std::array<c4::assets::Vertex, 4> expected{{
        {0, -78, 12.5f, .5f / 156, .5f / 25, 1, 0, 0},
        {0, 78, 12.5f, 155.5f / 156, .5f / 25, 1, 0, 0},
        {0, 78, -12.5f, 155.5f / 156, 24.5f / 25, 1, 0, 0},
        {0, -78, -12.5f, .5f / 156, 24.5f / 25, 1, 0, 0}
    }};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto& actual = vertices[i];
        const auto& wanted = expected[i];
        assert(actual.x == wanted.x && actual.y == wanted.y && actual.z == wanted.z);
        assert(actual.u == wanted.u && actual.v == wanted.v);
        assert(actual.nx == wanted.nx && actual.ny == wanted.ny && actual.nz == wanted.nz);
    }
    const std::array<std::uint32_t, 6> fan{2, 1, 0, 3, 2, 0};
    assert(c4::ui::original_view_sprite_indices == fan);
}
void check_hud_visibility_restore(c4::ui::NativeHud& hud) {
    c4::game::SharedState state;
    c4::game::FrameTiming timing;
    timing.originaltime_step_ticks = c4::physics::v(.5);
    state.oyunici_panel_acik = c4::physics::v(1);
    state.ghost_reset_ok = c4::physics::v(0);
    state.hud_speed_visible = false;
    state.gameover_panel_acik = c4::physics::v(1);
    state.turbo_ekle_miktar = c4::physics::v(150);
    state.active_ghost = true;
    state.checkpoint_event_counter = 1;
    state.checkpoint_event_no = 1;
    state.checkpoint_delta_ticks = c4::physics::v(0);
    hud.update(state, timing);
    const std::array<bool, 16> expected{
        true, true, true, true, true, true, true, false,
        false, true, false, false, true, true, false, true
    };
    assert(hud.source_visibility() == expected);
    ++state.hud_off_event_counter;
    hud.sync_source_visibility(state);
    for (std::size_t i = 0; i < expected.size(); ++i)
        assert(hud.source_visibility()[i] == (i == 12 || i == 13)); // These two slots are INVISIBLE.
    hud.restore_source_panels(state);
    assert(hud.source_visibility() == expected); // Restore saved mixed flags, never blanket-enable.
    hud.update(state, timing); // Blocked source loops cannot clobber the restored flags.
    assert(hud.source_visibility() == expected);
    state.hud_speed_visible = true;
    hud.sync_source_visibility(state);
    const auto camera_on = hud.source_visibility();
    assert(camera_on[7] && camera_on[8] && camera_on[10] && camera_on[11]);
    assert(!camera_on[12] && !camera_on[13]);
    state.hud_speed_visible = false; // OFF producer also clears the camera proxy in this same ingress.
    ++state.hud_off_event_counter;
    hud.sync_source_visibility(state);
    hud.restore_source_panels(state);
    assert(hud.source_visibility() == camera_on); // Snapshot must precede proxy-edge handling.
}
void check_source_panel_boundary(c4::ui::MenuPanel& fresh, c4::ui::MenuPanel& retained,
                                 c4::ui::Resources& resources) {
    using Screen = c4::ui::MenuPanel::Screen;
    using Command = c4::game::UiCommand;
    c4::game::SharedState state;
    c4::game::FrameTiming timing;
    timing.originaltime_step_ticks = c4::physics::v(.5);
    std::vector<c4::ui::Quad> quads;
    c4::ui::Painter painter(resources, quads);
    const auto step = [&](c4::ui::MenuPanel& panel) {
        timing.seconds_deadlines += 1. / 32;
        ++timing.frame_index;
        panel.update(state, timing);
        panel.finish_ui_pass(c4::ui::UiPass::AfterWorld);
        quads.clear();
        panel.draw(painter);
    };
    const auto ready = [&](c4::ui::MenuPanel& panel, Screen screen) {
        for (int frame = 0; frame < 1000 && !panel.ready(); ++frame) step(panel);
        assert(panel.ready() && panel.screen() == screen);
        while (!panel.take_sound().empty()) {}
    };
    ready(fresh, Screen::main);
    ready(retained, Screen::main);
    int height = 0, row = 0;
    for (const auto name : {"mmbtn_yeni_oyun1.tga", "mmbtn_ayarlar1.tga",
                            "mmbtn_skor_liste1.tga", "mmbtn_ekip1.tga", "mmbtn_cikis1.tga"}) {
        height = std::max(height, row * 50 + static_cast<int>(resources.image(resources.find(name)).height));
        ++row;
    }
    const auto& exit_image = resources.image(resources.find("mmbtn_cikis1.tga"));
    c4::ui::MenuInput edge;
    edge.x = 221.25f + std::floor(exit_image.width * .625f) - .5f;
    edge.y = 181.25f + height * .625f + 1.f / 1024;
    edge.pointer_enabled = edge.pointer_inside = edge.pointer_pressed = true;
    edge.pointer_down = true;
    assert(fresh.input(edge) == Command::none);
    assert(fresh.ready() && fresh.screen() == Screen::main);
    assert(fresh.take_sound().empty()); // Global bbox rejects before the inclusive SAR10 child helper.

    auto hover = edge;
    hover.y = 181.25f + 200 * .625f + 1;
    hover.pointer_pressed = hover.pointer_down = false;
    assert(retained.input(hover) == Command::none);
    assert(retained.take_sound() == "ses_buton_1");
    assert(retained.take_sound().empty());
    assert(retained.input(edge) == Command::none);
    assert(!retained.ready());
    assert(retained.take_sound() == "ses_menu_carp"); // Previous selected Main bypasses its global bbox.
    assert(retained.take_sound().empty());
    ready(retained, Screen::exit); // Actual original Exit callback, not a synthetic command echo.
}
void check_authentic_tiers(const std::filesystem::path& root) {
    using namespace c4::ui;
    using c4::physics::v;
    using c4::physics::number;
    using Screen = MenuPanel::Screen;
    constexpr std::array<int,4> widths{640,800,1024,1280}, heights{480,600,768,1024};
    constexpr std::array<int,4> time_w{5,7,9,11}, time_h{10,14,19,23};
    constexpr std::array<int,4> digital_w{8,10,13,16}, digital_h{11,13,17,21};
    for (int tier = 0; tier < 4; ++tier) {
        Resources resources(root,std::nullopt,widths[tier],heights[tier]);
        assert(resources.tier() == tier && resources.canvas_width() == widths[tier]
               && resources.canvas_height() == heights[tier]);
        MenuPanel menu(resources);
        GraphicsPanel graphics(resources);
        NativeHud hud(resources);
        resources.load();
        check_graphics_requests(graphics,resources);
        const auto x = [&](int value) { return static_cast<float>(number(v(value)*v(resources.scale_x()))); };
        const auto y = [&](int value) { return static_cast<float>(number(v(value)*v(resources.scale_y()))); };
        std::vector<Quad> quads;
        Painter painter(resources,quads);
        c4::game::SharedState state;
        c4::game::FrameTiming timing;
        timing.originaltime_step_ticks = v(.5);
        const auto step = [&] {
            while (!menu.take_sound().empty()) {}
            timing.seconds_deadlines += 1./32;
            menu.update(state,timing);
            menu.finish_ui_pass(UiPass::AfterWorld);
            quads.clear();
            menu.draw(painter);
        };
        const auto ready = [&](Screen expected) {
            for (int frame = 0; frame < 1000 && !menu.ready(); ++frame) step();
            assert(menu.ready() && menu.screen() == expected);
        };
        const auto click = [&](float px, float py) {
            while (!menu.take_sound().empty()) {}
            MenuInput input;
            input.x = px; input.y = py; input.pointer_inside = input.pointer_enabled = true;
            input.pointer_pressed = input.pointer_down = true;
            const auto command = menu.input(input);
            // Match UI1/UI2 so hidden source panes expire before the next sampled click.
            menu.finish_ui_pass(UiPass::BeforeWorld);
            assert(menu.input(input) == c4::game::UiCommand::none);
            menu.finish_ui_pass(UiPass::AfterWorld);
            input.pointer_pressed = input.pointer_down = false;
            input.pointer_released = true;
            assert(menu.input(input) == c4::game::UiCommand::none);
            menu.finish_ui_pass(UiPass::BeforeWorld);
            assert(menu.input(input) == c4::game::UiCommand::none);
            menu.finish_ui_pass(UiPass::AfterWorld);
            return command;
        };
        const auto image = [&](std::string_view name) -> const Quad& {
            const auto id = resources.find(name);
            const auto it = std::find_if(quads.begin(),quads.end(),[&](const Quad& q) { return q.image_index == id; });
            assert(it != quads.end());
            return *it;
        };
        ready(Screen::main);
        const auto& new_game = image("mmbtn_yeni_oyun0.tga");
        assert(new_game.rect[0] == x(354) && new_game.rect[1] == y(290));
        const auto& car = image("menu_arac_gri.tga");
        const auto& car_image = resources.image(car.image_index);
        assert(car.rect[0]+car.rect[2] == widths[tier]-1);
        assert(car.rect[1]+car.rect[3] == heights[tier]-1);
        assert(car.rect[2] == car_image.width*static_cast<float>(number(v(resources.scale_x()))));
        assert(click(x(354)+5,y(340)+5) == c4::game::UiCommand::none);
        assert(menu.ready() && menu.screen() == Screen::main); // Layer-19 robot2 covers this button edge.
        const auto& settings_button = image("mmbtn_ayarlar0.tga");
        assert(click(settings_button.rect[0]+settings_button.rect[2]*.5f,
                     settings_button.rect[1]+settings_button.rect[3]*.5f) == c4::game::UiCommand::none);
        ready(Screen::settings);
        assert(click(x(354)+5,y(590)+5) == c4::game::UiCommand::none);
        ready(Screen::main);
        assert(click(x(354)+5,y(440)+5) == c4::game::UiCommand::none);
        ready(Screen::credits);
        for (int frame = 0; frame < 16; ++frame) step(); // RES2 starts with the authored blank lead-in clipped.
        const auto time_font = resources.find("time_beyazfont_" + std::to_string(tier) + ".tga");
        bool credits_glyph = false;
        for (const auto& quad : quads) {
            if (quad.image_index != time_font) continue;
            credits_glyph = true;
            assert(quad.rect[2] == time_w[tier] && quad.rect[3] <= time_h[tier]);
            assert(quad.rect[1] >= y(210)-.001f && quad.rect[1]+quad.rect[3] <= y(596)+.001f);
        }
        assert(credits_glyph);
        state.phase = c4::game::GamePhase::paused;
        step();
        assert(click(x(442)+5,y(436)+5) == c4::game::UiCommand::none);
        assert(menu.screen() == Screen::confirm_restart);
        quads.clear(); menu.draw(painter);
        const auto question = std::find_if(quads.begin(),quads.end(),[&](const Quad& q) {
            return q.image_index == time_font && q.source_order == source_ui_order(4,1660);
        });
        assert(question != quads.end() && question->rect[2] == time_w[tier]
               && question->rect[3] == time_h[tier] && question->rect[1] == y(356));
        const auto last_question = std::find_if(quads.rbegin(),quads.rend(),[&](const Quad& q) {
            return q.image_index == time_font && q.source_order == source_ui_order(4,1660);
        });
        constexpr std::array<int,4> question_x{470,476,485,490};
        assert(last_question != quads.rend());
        assert((question->rect[0]+last_question->rect[0]+time_w[tier])*.5f == x(question_x[tier]));
        assert(click(x(520)+5,y(436)+5) == c4::game::UiCommand::none);
        assert(menu.screen() == Screen::pause);
        state.phase = c4::game::GamePhase::results;
        state.game_over_ok = state.gameover_panel_acik = v(1);
        state.black_visible = state.finish_buttons_visible = true;
        state.black_alpha = v(100);
        state.timepanel_time_str = "ZAMAN 00:00.000";
        step();
        const auto result_glyph = std::find_if(quads.begin(),quads.end(),[&](const Quad& q) {
            return q.image_index == time_font && q.source_order == source_ui_order(4,1030);
        });
        assert(result_glyph != quads.end() && result_glyph->rect[0] == x(280)
               && result_glyph->rect[1] == y(356) && result_glyph->rect[2] == time_w[tier]
               && result_glyph->rect[3] == time_h[tier]);
        assert(click(x(365)+5,y(436)+5) == c4::game::UiCommand::none);
        assert(menu.screen() == Screen::confirm_restart);
        assert(click(x(520)+5,y(436)+5) == c4::game::UiCommand::none);
        assert(menu.screen() == Screen::results);
        state.phase = c4::game::GamePhase::race;
        state.gameover_panel_acik = v(0);
        state.oyunici_panel_acik = v(1);
        state.ghost_reset_ok = v(0);
        state.hud_speed_visible = true;
        state.turbo_ekle_miktar = v(150);
        state.arac_ivme = v(0); state.arac_vites_no = v(0);
        state.timepanel_time_str = "ZAMAN 00:00.000";
        hud.update(state,timing);
        hud.update(state,timing); // Complete any preceding saved-visibility source wait before ticking motion.
        quads.clear(); hud.draw(painter);
        const auto digital_font = resources.find("font_digital8_" + std::to_string(tier) + ".bmp");
        unsigned digital_glyphs = 0;
        for (const auto& quad : quads) {
            if (quad.image_index == digital_font) {
                ++digital_glyphs;
                assert(quad.rect[2] == digital_w[tier] && quad.rect[3] == digital_h[tier]);
                const unsigned code = quad.source_order == source_ui_order(1,94) ? 48 : 78;
                assert(quad.source_order == source_ui_order(1,94) || quad.source_order == source_ui_order(1,112));
                const auto& sheet = resources.image(digital_font);
                const auto columns = sheet.width/static_cast<unsigned>(digital_w[tier]);
                assert(columns == 32);
                const auto left = (code%columns)*static_cast<unsigned>(digital_w[tier]);
                const auto top = (code/columns)*static_cast<unsigned>(digital_h[tier]);
                assert((quad.uv == std::array<float,4>{
                    static_cast<float>(left)/sheet.width,static_cast<float>(top)/sheet.height,
                    static_cast<float>(left+digital_w[tier])/sheet.width,
                    static_cast<float>(top+digital_h[tier])/sheet.height}));
                unsigned visible_pixels = 0;
                for (int row = 0; row < digital_h[tier]; ++row)
                    for (int column = 0; column < digital_w[tier]; ++column)
                        visible_pixels += sheet.rgba[((top+row)*sheet.width+left+column)*4+3] != 0;
                assert(visible_pixels > 0); // Real selected glyph pixels, not an ASCII-offset assumption.
            }
        }
        assert(digital_glyphs == 2); // Actual selected sheets, speed 0 and neutral N.
        const auto& hud_panels = hud.source_panels();
        assert(hud_panels[0].x == v(widths[tier]-1) && hud_panels[0].y == v(heights[tier]-1));
        assert(hud_panels[0].sx == -v(resources.scale_x()) && hud_panels[0].sy == -v(resources.scale_y()));
        assert(hud_panels[2].sx == v(1)*(v(150)/v(300))*v(resources.scale_x()));
        assert(hud_panels[5].x == v(325)*v(resources.scale_x()) && hud_panels[5].sy == v(resources.scale_y()));
        const auto initial_roll = hud.source_needle_roll();
        state.arac_ivme = v(11); state.arac_vites_no = v(3);
        hud.update(state,timing);
        assert(hud.source_needle_roll() < initial_roll);
        quads.clear(); hud.draw(painter);
        unsigned moving_glyphs = 0;
        for (const auto& quad : quads) {
            if (quad.image_index != digital_font) continue;
            ++moving_glyphs;
            const unsigned code = quad.source_order == source_ui_order(1,94) ? 56 : 51; // speed 8, gear 3.
            const auto& sheet = resources.image(digital_font);
            assert(quad.uv[0] == static_cast<float>((code%32)*digital_w[tier])/sheet.width);
            assert(quad.uv[1] == static_cast<float>((code/32)*digital_h[tier])/sheet.height);
        }
        assert(moving_glyphs == 2);
        for (const auto& panel : hud.source_panels()) {
            if (!panel.visible) continue;
            assert(panel.sx != v(0) && panel.sy != v(0));
        }
        const auto viewport = Viewport::fit(1920,1080,widths[tier],heights[tier]);
        float mapped_x{}, mapped_y{};
        assert(viewport.map(viewport.x+x(354)*viewport.scale,
                            viewport.y+y(290)*viewport.scale,mapped_x,mapped_y));
        assert(std::abs(mapped_x-x(354)) < .001f && std::abs(mapped_y-y(290)) < .001f);
        const auto resized = Viewport::fit(900,1200,widths[tier],heights[tier]);
        assert(resized.map(resized.x+x(354)*resized.scale,
                           resized.y+y(290)*resized.scale,mapped_x,mapped_y));
        assert(std::abs(mapped_x-x(354)) < .001f && std::abs(mapped_y-y(290)) < .001f);
        assert(resources.tier() == tier); // Resizing maps the immutable launch canvas.
        check_hud_visibility_restore(hud);
    }
    assert(Resources(root,std::nullopt,1280,720).tier() == 1);
    assert(Resources(root,std::nullopt,1920,1000).tier() == 2);
    assert(Resources(root,std::nullopt,1023,768).tier() == 1);
    assert(Resources(root,std::nullopt,1280,1023).tier() == 2);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <imported-asset-root>\n";
        return 2;
    }
    try {
        TemporaryDirectory temporary;
        Environment state_home("XDG_STATE_HOME", temporary.path);
        Environment data_home("XDG_DATA_HOME", temporary.path);
        const auto settings_path = temporary.path / "citroen-c4-robot" / "mat.rc4";
        std::filesystem::create_directories(settings_path.parent_path());
        {
            std::ofstream seed(settings_path);
            seed << "100 0 1 0 0 666 ";
            seed.close();
            assert(seed);
        }

        c4::ui::Resources resources(argv[1]);
        c4::ui::MenuPanel menu(resources);
        c4::ui::GraphicsPanel graphics(resources);
        c4::ui::NativeHud hud(resources);
        c4::ui::MenuPanel fresh_boundary(resources), retained_boundary(resources);
        resources.load(); // Real imported images determine pointer hitboxes.
        check_graphics_requests(graphics, resources);
        check_source_view_sprite();
        check_hud_visibility_restore(hud);
        check_source_panel_boundary(fresh_boundary, retained_boundary, resources);
        check_authentic_tiers(argv[1]);
        std::vector<c4::ui::Quad> quads;
        c4::ui::Painter painter(resources, quads);
        using Screen = c4::ui::MenuPanel::Screen;
        using Command = c4::game::UiCommand;
        c4::game::SharedState state;
        c4::game::FrameTiming timing;
        timing.originaltime_step_ticks = c4::physics::v(.5);
        const auto drain_sound = [&] {
            while (!menu.take_sound().empty()) {}
        };
        const auto step = [&] {
            drain_sound();
            timing.seconds_deadlines += 1. / 32;
            ++timing.frame_index;
            menu.update(state, timing);
            menu.finish_ui_pass(c4::ui::UiPass::AfterWorld);
            quads.clear();
            menu.draw(painter);
        };
        c4::ui::MenuInput pointer_sample;
        const auto click = [&](float x, float y, bool pointer_enabled = true) {
            drain_sound();
            auto& input = pointer_sample;
            input = {};
            input.x = x; input.y = y; input.pointer_inside = true;
            input.pointer_enabled = pointer_enabled;
            input.pointer_down = true;
            input.pointer_pressed = true;
            const auto command = menu.input(input);
            // One hardware sample feeds UI1 and UI2 before the next sampled release.
            menu.finish_ui_pass(c4::ui::UiPass::BeforeWorld);
            assert(menu.input(input) == Command::none);
            menu.finish_ui_pass(c4::ui::UiPass::AfterWorld);
            input.pointer_pressed = false; input.pointer_released = true;
            input.pointer_down = false;
            assert(menu.input(input) == Command::none);
            menu.finish_ui_pass(c4::ui::UiPass::BeforeWorld);
            assert(menu.input(input) == Command::none);
            menu.finish_ui_pass(c4::ui::UiPass::AfterWorld);
            return command;
        };
        const auto poll_released_pointer = [&] {
            pointer_sample.pointer_enabled = state.mouse_mode != c4::physics::v(0);
            pointer_sample.pointer_pressed = pointer_sample.pointer_released = false;
            for (const auto pass : {c4::ui::UiPass::BeforeWorld, c4::ui::UiPass::AfterWorld}) {
                assert(menu.input(pointer_sample) == Command::none);
                menu.finish_ui_pass(pass);
            }
        };
        const auto until_ready = [&](Screen screen) {
            const int color = menu.settings().color;
            const int ghost = menu.settings().ghost;
            int frames = 0;
            while (!menu.ready() && frames++ < 1000) {
                // Inputs cannot dispatch callbacks or change settings in an animation.
                c4::ui::MenuInput blocked;
                blocked.x = 354 * .625f + 5; blocked.y = 290 * .625f + 5;
                blocked.pointer_inside = blocked.pointer_pressed = true;
                blocked.tab = blocked.enter = blocked.right = true;
                assert(menu.input(blocked) == Command::none);
                assert(menu.settings().color == color && menu.settings().ghost == ghost);
                step();
                assert(menu.input({}) == Command::none);
            }
            assert(menu.ready() && menu.screen() == screen);
        };
        const auto tab = [&] {
            drain_sound();
            assert(menu.input({.tab = true}) == Command::none);
        };
        const auto enter = [&] {
            drain_sound();
            return menu.input({.enter = true});
        };

        assert(menu.screen() == Screen::main && !menu.ready());
        assert(!menu.take_mouse_mode_write());
        assert(!menu.starts_menu_video(timing));
        assert(!menu.cursor_visible());
        assert(menu.settings().volume == 100 && menu.settings().brightness == 0);
        assert(menu.settings().music == 1 && menu.settings().color == 0 && menu.settings().ghost == 0);
        for (const int choice : {0, 1, 2}) {
            menu.set_selected_ghost(choice);
            assert(menu.settings().ghost == choice);
        }
        for (const int invalid : {-1, 3}) {
            bool rejected = false;
            try {
                menu.set_selected_ghost(invalid);
            } catch (const std::out_of_range&) {
                rejected = true;
            }
            assert(rejected && menu.settings().ghost == 2);
        }
        menu.set_selected_ghost(0);
        assert(enter() == Command::none);
        assert(click(354 * .625f + 5, 290 * .625f + 5) == Command::none);
        assert(menu.screen() == Screen::main && !menu.ready());
        step();
        assert(!menu.ready() && !menu.cursor_visible());
        assert(!menu.take_mouse_mode_write());
        for (int frame = 0; frame < 15; ++frame) {
            step();
            assert(!menu.take_mouse_mode_write());
            assert(!menu.starts_menu_video(timing));
        }
        auto resume_timing = timing;
        resume_timing.seconds_deadlines += 1. / 32;
        ++resume_timing.frame_index;
        assert(menu.starts_menu_video(resume_timing));
        step(); // First update's time + the source half-second startup deadline.
        assert(!menu.starts_menu_video(resume_timing)); // Predicate does not re-fire after source consumption.
        assert(menu.take_mouse_mode_write() == c4::physics::v(2));
        assert(!menu.take_mouse_mode_write() && !menu.ready());
        until_ready(Screen::main);
        assert(menu.cursor_visible());
        tab(); tab(); // Main Settings is the second original button.
        assert(enter() == Command::none);
        until_ready(Screen::settings);
        // Click the authored arrows as rendered, not a synthetic point before their bitmap.
        const auto volume_center = [&](bool increase) {
            quads.clear();
            menu.draw(painter);
            const std::array maps{
                resources.find(increase ? "mmbtn_sag_ok0.tga" : "mmbtn_sol_ok0.tga"),
                resources.find(increase ? "mmbtn_sag_ok1.tga" : "mmbtn_sol_ok1.tga"),
                resources.find(increase ? "mmbtn_sag_ok2.tga" : "mmbtn_sol_ok2.tga")};
            const auto arrow = std::find_if(quads.begin(), quads.end(), [&](const auto& quad) {
                return std::find(maps.begin(), maps.end(), quad.image_index) != maps.end();
            });
            assert(arrow != quads.end() && arrow->rect[2] > 0 && arrow->rect[3] > 0);
            return std::array{std::round(arrow->rect[0] + arrow->rect[2] * .5f),
                              std::round(arrow->rect[1] + arrow->rect[3] * .5f)};
        };
        const auto volume_down = volume_center(false), volume_up = volume_center(true);
        const auto click_volume = [&](bool increase) {
            const auto& position = increase ? volume_up : volume_down;
            assert(click(position[0], position[1]) == Command::none);
        };
        click_volume(false); click_volume(false);
        assert(menu.settings().volume == 80);
        click_volume(true);
        assert(menu.settings().volume == 90);
        click_volume(false);
        assert(menu.settings().volume == 80);
        c4::ui::MenuInput sampled;
        sampled.x = volume_down[0]; sampled.y = volume_down[1];
        sampled.pointer_inside = sampled.pointer_enabled = sampled.pointer_pressed = true;
        sampled.pointer_down = true;
        assert(menu.input(sampled) == Command::none); // UI1: normal held press.
        menu.finish_ui_pass(c4::ui::UiPass::BeforeWorld);
        assert(menu.input(sampled) == Command::none); // UI2: same edge, already held.
        menu.finish_ui_pass(c4::ui::UiPass::AfterWorld);
        assert(menu.settings().volume == 70);
        sampled.pointer_pressed = false;
        assert(menu.input(sampled) == Command::none); // A later held poll cannot repeat the callback.
        assert(menu.settings().volume == 70);
        sampled.pointer_down = false;
        assert(menu.input(sampled) == Command::none);
        click_volume(true);
        assert(menu.settings().volume == 80);
        // Ordinary down+up between source samples yields edge0/held0: no callback in either pass.
        assert(menu.input(sampled) == Command::none);
        menu.finish_ui_pass(c4::ui::UiPass::BeforeWorld);
        assert(menu.input(sampled) == Command::none);
        menu.finish_ui_pass(c4::ui::UiPass::AfterWorld);
        assert(menu.settings().volume == 80);
        sampled.pointer_pressed = sampled.pointer_down = true;
        assert(menu.input(sampled) == Command::none);
        assert(menu.settings().volume == 70);
        sampled.pointer_enabled = sampled.pointer_pressed = sampled.pointer_down = false;
        assert(menu.input(sampled) == Command::none);
        assert(menu.settings().volume == 70);
        sampled.pointer_enabled = true;
        assert(menu.input(sampled) == Command::none); // Re-enabled released poll only consumes held history.
        assert(menu.input(sampled) == Command::none);
        assert(menu.settings().volume == 70);
        for (int press = 1; press <= 8; ++press) {
            click_volume(false);
            assert(menu.settings().volume == std::max(0, 70 - press * 10));
        }
        for (int press = 1; press <= 11; ++press) {
            click_volume(true);
            assert(menu.settings().volume == std::min(100, press * 10));
        }
        const auto before_graphics = menu.settings();
        assert(!menu.take_graphics_open_request());
        quads.clear();
        menu.draw(painter); // Query uses the real prior-rendered original/settings layout.
        c4::ui::MenuInput graphics_press;
        graphics_press.x = 226.25f; graphics_press.y = 217;
        graphics_press.pointer_inside = graphics_press.pointer_enabled = true;
        graphics_press.pointer_pressed = graphics_press.pointer_down = true;
        assert(menu.would_open_graphics(graphics_press));
        assert(menu.would_open_graphics(graphics_press)); // Prediction cannot consume history or queue.
        assert(!menu.take_graphics_open_request());
        assert(click(221.25f + 5, 212.f + 5) == Command::none);
        assert(menu.take_graphics_open_request());
        assert(!menu.take_graphics_open_request());
        assert(menu.screen() == Screen::settings && menu.ready());
        assert(menu.settings().volume == before_graphics.volume &&
               menu.settings().brightness == before_graphics.brightness &&
               menu.settings().music == before_graphics.music &&
               menu.settings().color == before_graphics.color &&
               menu.settings().ghost == before_graphics.ghost);
        drain_sound();
        assert(menu.input({.tab = true, .shift = true}) == Command::none); // Native Graphics5 -> original Back4.
        assert(enter() == Command::none);
        until_ready(Screen::main);
        tab();
        assert(enter() == Command::none);
        assert(menu.cursor_visible()); // Animated menu panels keep the source cursor enabled.
        assert(!menu.ready());
        until_ready(Screen::color);
        tab(); tab(); tab(); // Left, Right, Next, Back.
        assert(enter() == Command::none);
        assert(!menu.ready());
        until_ready(Screen::ghost);
        assert(!menu.take_mouse_mode_write()); // Ordinary page transitions do not write mouse mode.

        assert(click(354 * .625f + 5, (170 + 170) * .625f + 5) == Command::none);
        assert(menu.take_sound().empty()); // Offline server-ghost slot has no callback or hover sound.
        assert(menu.screen() == Screen::ghost && menu.settings().ghost == 0);
        assert(!state.local_ghost_available);
        tab(); // Local remains selectable even without a saved local track.
        assert(enter() == Command::none); // Never dispatch select_local from this menu.
        assert(menu.settings().ghost == 1 && state.selected_ghost == c4::physics::v(0));
        tab(); // Focus No Ghost, without activating it.
        assert(menu.settings().ghost == 1);
        tab(); // Start.
        assert(enter() == Command::none);
        assert(!menu.ready() && !menu.loading_visible());
        assert(!menu.take_music_stop_request());
        assert(menu.input({}) == Command::none);
        assert(menu.take_continuation_command() == Command::none);

        // Source nudge354->334 takes four iterations, then eject334->690 takes ten;
        // the endpoint iteration is shared and Start runs the first iteration immediately.
        // Thus post-callback updates1..11 cannot play; update12 reaches the eject clamp.
        for (int frame = 0; frame < 11; ++frame) {
            step();
            assert(menu.input({}) == Command::none);
            assert(menu.take_continuation_command() == Command::none);
            assert(!menu.loading_visible());
            assert(!menu.take_music_stop_request());
        }
        for (int update = 12; update <= 17; ++update) {
            step();
            assert(menu.loading_visible());
            assert(menu.take_continuation_command() == Command::none);
            assert(menu.take_music_stop_request() == (update == 12));
            assert(!menu.take_music_stop_request());
            if (update == 13) {
                assert(menu.take_mouse_mode_write() == c4::physics::v(0));
                assert(menu.take_video_stop_request());
                assert(!menu.take_video_stop_request());
            } else {
                assert(!menu.take_mouse_mode_write());
                assert(!menu.take_video_stop_request());
            }
            std::ifstream saved(settings_path);
            int volume{}, brightness{}, music{}, color{}, ghost{}, sentinel{};
            assert(saved >> volume >> brightness >> music >> color >> ghost >> sentinel);
            assert(ghost == (update < 17 ? 0 : 1)); // SAVE belongs to the penultimate source wait.
        }
        Command command = Command::none;
        for (int frame = 0; frame < 1000 && command == Command::none; ++frame) {
            step();
            command = menu.take_continuation_command();
            assert(command == Command::none || command == Command::play);
            assert(frame == 0 && command == Command::play); // Exactly post-callback update18.
        }
        assert(command == Command::play && menu.loading_visible());
        assert(!menu.cursor_visible());
        assert(menu.take_continuation_command() == Command::none); // Continuation command is consumed exactly once.
        {
            std::ifstream saved(settings_path);
            int volume{}, brightness{}, music{}, color{}, ghost{}, sentinel{};
            assert(saved >> volume >> brightness >> music >> color >> ghost >> sentinel);
            assert(volume == 100 && brightness == 0 && music == 1 && color == 0);
            assert(ghost == 1 && sentinel == 666);
            saved >> std::ws;
            assert(saved.eof());
        }
        state.loading_visible = true;
        state.loading_stage = 0;
        state.loading_progress = c4::physics::v(20);
        step();
        quads.clear();
        menu.draw(painter);
        assert(quads.size() == 5 && !menu.cursor_visible()); // Source black99 remains under BG100.
        const auto& loading_bar = quads.back();
        assert(loading_bar.rect[1] == 734 * .625f);
        const float expected_width = static_cast<float>(c4::physics::number(
            c4::physics::v(1) + c4::physics::v(8.23) * c4::physics::v(20) * c4::physics::v(.625)));
        assert(loading_bar.rect[2] == expected_width && loading_bar.rect[3] == 10 * .625f);
        assert(loading_bar.color[0] == 1 && loading_bar.color[1] == 128.f / 255);
        assert(loading_bar.color[2] == 0); // Original BGR vec(0,128,255) is orange, not blue.
        assert(resources.image(loading_bar.image_index).width == 1);
        assert(resources.image(loading_bar.image_index).height == 1);
        state.loading_stage = 4; // tekrarLevel_yukle: direct 10.23 bar, then exact full canvas.
        step();
        quads.clear();
        menu.draw(painter);
        assert(quads.back().rect[2] == static_cast<float>(c4::physics::number(
            c4::physics::v(1) + c4::physics::v(10.23) * c4::physics::v(20) * c4::physics::v(.625))));
        state.loading_progress = c4::physics::v(101.25);
        step();
        quads.clear();
        menu.draw(painter);
        assert(quads.back().rect[2] == 1024 * .625f);
        state.loading_visible = false;

        state.phase = c4::game::GamePhase::paused;
        state.mouse_mode = c4::physics::v(2); // Runtime owns the raw-ESC smart-pause callback.
        step();
        assert(menu.screen() == Screen::pause && menu.ready());
        assert(!menu.take_mouse_mode_write());
        assert(click(235.f + 5, 245.f + 5) == Command::none);
        assert(menu.take_graphics_open_request());
        assert(!menu.take_graphics_open_request());
        assert(menu.screen() == Screen::pause && menu.ready() && menu.settings().ghost == 1);
        tab(); tab(); tab(); tab(); // Graphics3 -> Resume0 -> Restart1 -> Menu2 -> Graphics3.
        assert(enter() == Command::none);
        assert(menu.take_graphics_open_request());
        assert(!menu.take_graphics_open_request());
        assert(menu.screen() == Screen::pause && menu.ready());
        for (int repeat = 0; repeat < 3; ++repeat) {
            assert(menu.input({.escape_pressed = true}) == Command::none);
            assert(menu.input({.escape_released = true}) == Command::none);
            assert(menu.screen() == Screen::pause);
        }
        assert(click((288 + 154) * .625f + 5, 436 * .625f + 5) == Command::none);
        assert(menu.screen() == Screen::confirm_restart);
        assert(menu.input({.escape_pressed = true, .escape_released = true}) == Command::none);
        assert(menu.screen() == Screen::confirm_restart);
        assert(click((365 + 155) * .625f + 5, 436 * .625f + 5) == Command::none);
        assert(menu.screen() == Screen::pause && menu.ready());
        step();
        assert(menu.screen() == Screen::pause); // No restores pause, not gameplay.
        assert(!menu.take_mouse_mode_write()); // No and repeated pause updates do not reopen SMP.

        state.help_visible = true;
        step();
        assert(menu.screen() == Screen::help);
        assert(!menu.cursor_visible());
        assert(menu.input({.escape_released = true}) == Command::none);
        assert(menu.screen() == Screen::help);
        assert(menu.input({.escape_pressed = true}) == Command::none);
        assert(menu.screen() == Screen::help);
        assert(menu.input({.escape_released = true}) == Command::none);
        assert(menu.screen() == Screen::help); // Runtime exclusively owns help closure.
        assert(menu.input({.escape_released = true}) == Command::none);
        state.help_visible = false;
        state.help_visible = true; // Same-frame runtime close plus F1 reopen.
        step();
        assert(menu.input({.escape_released = true}) == Command::none);
        assert(menu.screen() == Screen::help);
        state.help_visible = false;
        step();
        assert(menu.screen() == Screen::hidden);
        assert(!menu.take_mouse_mode_write()); // Help input and visibility belong to Runtime.
        state.phase = c4::game::GamePhase::results;
        state.game_over_ok = c4::physics::v(1);
        state.gameover_panel_acik = c4::physics::v(1);
        state.finish_buttons_visible = false;
        state.mouse_mode = c4::physics::v(0); // Race/result delay precedes GOP's button/mouse-mode handoff.
        state.black_visible = true;
        state.black_alpha = c4::physics::v(50);
        step();
        assert(menu.screen() == Screen::results && !menu.ready());
        assert(!menu.cursor_visible());
        quads.clear();
        menu.draw(painter);
        assert(quads.size() == 1);
        assert(quads.front().image_index == resources.find("GO_sonuc_fon0.tga"));
        assert(quads.front().color[3] == .5f);
        tab();
        assert(enter() == Command::none);
        assert(click(365 * .625f + 5, 436 * .625f + 5,
                     state.mouse_mode != c4::physics::v(0)) == Command::none);
        assert(menu.input({.escape_pressed = true, .escape_released = true}) == Command::none);
        assert(menu.screen() == Screen::results);
        state.finish_buttons_visible = true;
        state.mouse_mode = c4::physics::v(2); // Source GOP enables pointer input with the result buttons.
        step();
        poll_released_pointer();
        assert(menu.screen() == Screen::results && menu.ready());
        assert(menu.cursor_visible());
        assert(!menu.take_mouse_mode_write()); // Runtime owns the result-buttons mode write.
        assert(menu.input({.escape_pressed = true}) == Command::none);
        assert(menu.input({.escape_released = true}) == Command::none);
        assert(menu.screen() == Screen::results);
        assert(click(365 * .625f + 5, 436 * .625f + 5) == Command::none);
        assert(menu.screen() == Screen::confirm_restart);
        assert(click((365 + 155) * .625f + 5, 436 * .625f + 5) == Command::none);
        assert(menu.screen() == Screen::results && menu.ready());
        assert(click((365 + 155) * .625f + 5, 436 * .625f + 5) == Command::none);
        assert(menu.screen() == Screen::confirm_menu);
        assert(click((365 + 155) * .625f + 5, 436 * .625f + 5) == Command::none);
        assert(menu.screen() == Screen::results && menu.ready());
        state.finish_buttons_visible = false;
        state.mouse_mode = c4::physics::v(0);
        step();
        assert(!menu.ready());
        assert(click(365 * .625f + 5, 436 * .625f + 5,
                     state.mouse_mode != c4::physics::v(0)) == Command::none);
        assert(menu.screen() == Screen::results);
        assert(!menu.take_menu_return_view_flags()); // Ordinary results, pause, and No paths never emit it.
        state.phase = c4::game::GamePhase::paused;
        state.mouse_mode = c4::physics::v(2);
        step();
        assert(!menu.take_mouse_mode_write());
        assert(click(288 * .625f + 5, 436 * .625f + 5) == Command::resume);
        assert(!menu.take_mouse_mode_write()); // Runtime, not the Resume callback, owns mode zero.
        for (const auto phase : {c4::game::GamePhase::paused, c4::game::GamePhase::results}) {
            for (const bool restart : {true, false}) {
                state.phase = c4::game::GamePhase::race;
                state.mouse_mode = c4::physics::v(0);
                step();
                assert(!menu.take_mouse_mode_write());
                state.phase = phase;
                state.finish_buttons_visible = true;
                state.mouse_mode = c4::physics::v(2); // Authoritative Runtime input, not a UI event.
                step();
                poll_released_pointer();
                assert(!menu.take_mouse_mode_write());
                quads.clear();
                menu.draw(painter);
                const std::array action_maps{
                    resources.find(restart ? "GO_sonuc_restart0.tga" : "GO_sonuc_mmenu0.tga"),
                    resources.find(restart ? "GO_sonuc_restart1.tga" : "GO_sonuc_mmenu1.tga")};
                const auto action_button = std::find_if(quads.begin(), quads.end(), [&](const auto& quad) {
                    return std::find(action_maps.begin(), action_maps.end(), quad.image_index) != action_maps.end();
                });
                assert(action_button != quads.end());
                assert(click(std::round(action_button->rect[0] + action_button->rect[2] * .5f),
                             std::round(action_button->rect[1] + action_button->rect[3] * .5f),
                             state.mouse_mode != c4::physics::v(0)) == Command::none);
                assert(!menu.take_mouse_mode_write());
                assert(menu.screen() == (restart ? Screen::confirm_restart : Screen::confirm_menu));
                assert(click(365 * .625f + 5, 436 * .625f + 5) == Command::none);
                assert(menu.take_mouse_mode_write() == c4::physics::v(0));
                state.mouse_mode = c4::physics::v(0);
                assert(!menu.take_mouse_mode_write() && !menu.ready());
                assert(!menu.take_menu_return_view_flags()); // YES starts its fade; no return-tail event yet.
                assert(!menu.take_results_hud_restore());
                assert(!menu.take_music_stop_request()); // Physical YES does not stop before its fade.
                Command deferred = Command::none;
                int flags_frame = -1;
                int hud_frame = -1;
                int music_stop_frame = -1;
                double music_stop_time = 0;
                for (int frame = 0; frame < 1000 && deferred == Command::none; ++frame) {
                    step();
                    assert(!menu.take_mouse_mode_write());
                    deferred = menu.take_continuation_command();
                    if (menu.take_music_stop_request()) {
                        assert(music_stop_frame == -1 && frame == 8 && deferred == Command::none);
                        music_stop_frame = frame;
                        music_stop_time = timing.seconds_deadlines;
                        assert(!menu.take_music_stop_request());
                        quads.clear();
                        menu.draw(painter);
                        const auto curtain = std::find_if(quads.begin(), quads.end(), [](const auto& quad) {
                            return quad.layer == 99;
                        });
                        assert(curtain != quads.end() && curtain->color[3] == 1);
                    }
                    if (deferred != Command::none) {
                        assert(music_stop_frame >= 0 && frame > music_stop_frame);
                        const double extra_frame = (!restart || phase == c4::game::GamePhase::results) ? 1. / 32 : 0;
                        assert(timing.seconds_deadlines - music_stop_time == (restart ? 1 : .5) + extra_frame);
                    }
                    if (menu.take_results_hud_restore()) {
                        assert(phase == c4::game::GamePhase::results && hud_frame == -1);
                        assert(deferred == Command::none);
                        hud_frame = frame;
                        assert(!menu.take_results_hud_restore());
                    }
                    if (menu.take_menu_return_view_flags()) {
                        assert(!restart && flags_frame == -1 && deferred == Command::none);
                        flags_frame = frame;
                        if (phase == c4::game::GamePhase::results) assert(hud_frame == frame);
                        assert(!menu.take_menu_return_view_flags());
                        quads.clear();
                        menu.draw(painter);
                        assert(quads.size() == 1 && quads.front().layer == 99);
                        assert(quads.front().color[3] == 1);
                    }
                    if (flags_frame >= 0 && frame > flags_frame)
                        assert(frame == flags_frame + 1 && deferred == Command::menu);
                    if (hud_frame >= 0 && frame > hud_frame)
                        assert(frame == hud_frame + 1 && deferred == (restart ? Command::restart : Command::menu));
                }
                assert(deferred == (restart ? Command::restart : Command::menu));
                assert(restart ? flags_frame == -1 : flags_frame >= 0);
                assert(!menu.take_menu_return_view_flags());
                assert(phase == c4::game::GamePhase::results ? hud_frame >= 0 : hud_frame == -1);
                assert(!menu.take_results_hud_restore());
                assert(menu.take_continuation_command() == Command::none);
                assert(!menu.take_mouse_mode_write());
            }
        }
        std::cout << "UI transitions, offline selection, persistence, pause/help/results gates passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "UI transitions check: " << error.what() << '\n';
        return 1;
    }
}
