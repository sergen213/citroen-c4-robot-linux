#include "MenuPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <stdexcept>
#include <utility>

namespace c4::ui {
namespace {
constexpr std::array<int,4> font_width{5,7,9,11}, font_height{10,14,19,23};
constexpr std::array<double,4> font_y_ratio{1,1.4,1.9,2.3};
constexpr std::array<int,4> credits_min_y{5,32,32,32}, question_x{470,476,485,490};
constexpr std::array<double,4> tile_x_adjust{.002,.002,0,.002}, tile_y_adjust{0,.001,0,.002};
constexpr std::array<double,4> video_adjust{.005,.008,0,.005};
using physics::Var;
using physics::v;
using physics::number;
using physics::var_abs;
using physics::var_min;
using physics::var_max;
using physics::var_clamp;
using physics::var_modulo;
float pixel(Var value) { return static_cast<float>(number(value)); }
float opacity(Var percentage) { return static_cast<float>(std::clamp(number(percentage)/100,0.,1.)); }
constexpr std::array<std::string_view, 10> colors{
    "gri", "kirmizi", "kul", "sari", "mavi", "deve", "yesil", "beyaz", "siyah", "turuncu"};
struct Art { std::string_view idle, hover, pressed; };
constexpr std::array<Art, 21> button_art{{
    {"mmbtn_yeni_oyun0.tga","mmbtn_yeni_oyun1.tga","mmbtn_yeni_oyun1.tga"},
    {"mmbtn_ayarlar0.tga","mmbtn_ayarlar1.tga","mmbtn_ayarlar1.tga"},
    {"mmbtn_skor_liste0.tga","mmbtn_skor_liste1.tga","mmbtn_skor_liste1.tga"},
    {"mmbtn_ekip0.tga","mmbtn_ekip1.tga","mmbtn_ekip1.tga"},
    {"mmbtn_cikis0.tga","mmbtn_cikis1.tga","mmbtn_cikis1.tga"},
    {"mmbtn_geri0.tga","mmbtn_geri1.tga","mmbtn_geri1.tga"},
    {"mmbtn_ileri0.tga","mmbtn_ileri1.tga","mmbtn_ileri1.tga"},
    {"mmbtn_sol_ok0.tga","mmbtn_sol_ok1.tga","mmbtn_sol_ok2.tga"},
    {"mmbtn_sag_ok0.tga","mmbtn_sag_ok1.tga","mmbtn_sag_ok2.tga"},
    {"mmbtn_local0.tga","mmbtn_local1.tga","mmbtn_local2.tga"},
    {"mmbtn_istemiyor0.tga","mmbtn_istemiyor1.tga","mmbtn_istemiyor2.tga"},
    {"mmbtn_basla0.tga","mmbtn_basla1.tga","mmbtn_basla1.tga"},
    {"mmbtn_evet0.tga","mmbtn_evet1.tga","mmbtn_evet1.tga"},
    {"mmbtn_hayir0.tga","mmbtn_hayir1.tga","mmbtn_hayir1.tga"},
    {"GS_yukle_ok0.tga","GS_yukle_ok1.tga","GS_yukle_ok1.tga"},
    {"GO_sonuc_restart0.tga","GO_sonuc_restart1.tga","GO_sonuc_restart1.tga"},
    {"GO_sonuc_mmenu0.tga","GO_sonuc_mmenu1.tga","GO_sonuc_mmenu1.tga"},
    {"GO_sonuc_evet0.tga","GO_sonuc_evet1.tga","GO_sonuc_evet1.tga"},
    {"GO_sonuc_hayir0.tga","GO_sonuc_hayir1.tga","GO_sonuc_hayir1.tga"},
    {"mmbtn_local2.tga","mmbtn_local2.tga","mmbtn_local2.tga"},
    {"mmbtn_istemiyor2.tga","mmbtn_istemiyor2.tga","mmbtn_istemiyor2.tga"},
}};
constexpr std::string_view credits =
    "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\nCITROEN C4 ROBOT OYUNU\nPROGRAMLAMA\nYasin DEMİRDEN"
    "\nGRAFİK\nYakup DEMİRDEN\nOYUN DİZAYN\nMevlüt DİNÇ\nNecati DİNÇ\nYakup DEMİRDEN\nYasin DEMİRDEN"
    "\nYAPIMCI\nMevlüt DİNÇ\n\nTEŞEKKÜRLER\nCem SERMEN\nDevrim DİKOL\nHasan Fevzi ALTUNCU"
    "\nİsmet Yalım ALATLI\nMurat AFŞAR\nNamık Özgür SONER\nÖmer Kürşad KARAMAHMUTOĞLU\n"
    "\nÖZEL TEŞEKKÜR\nCitroen Türkiye'ye katkılarından\ndolayı teşekkür ederiz.\n"
    "\nwww.c4robot.com\nwww.citroen.com.tr\nwww.sobee.com.tr\nwww.2genstudyo.com\n"
    "\nTüm hakları saklıdır\n(c)2008\n";
constexpr std::string_view restart_question = "OYUNU TEKRAR BAŞLATMAK İSTEDİĞİNİZE EMİN MİSİNİZ?";
constexpr std::string_view menu_question = "OYUNU KAPATIP ANA MENEÜYE DÖNMEK İSTEDİĞİNİZE\nEMİN MİSİNİZ?";

std::filesystem::path settings_path() {
    if (const char* base = std::getenv("XDG_STATE_HOME"); base && *base)
        return std::filesystem::path(base) / "citroen-c4-robot/mat.rc4";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".local/state/citroen-c4-robot/mat.rc4";
    return {};
}
bool read_settings(const std::filesystem::path& path, UiSettings& settings) {
    std::ifstream in(path);
    in.imbue(std::locale::classic());
    std::array<double,6> fields{};
    for (auto& field : fields) if (!(in >> field) || !std::isfinite(field)) return false;
    if (fields[5] != 666 || fields[0] < 0 || fields[0] > 100 ||
        fields[1] < -50 || fields[1] > 50 || fields[2] < 0 || fields[2] > 1 ||
        fields[3] < 0 || fields[3] > 9 || fields[4] < 0 || fields[4] > 2) return false;
    settings = {static_cast<int>(fields[0]),static_cast<int>(fields[1]),static_cast<int>(fields[2]),
                static_cast<int>(fields[3]),static_cast<int>(fields[4])};
    if (settings.ghost == 2) settings.ghost = 1;
    return true;
}
float origin_y(MenuPanel::Screen screen) {
    if (screen == MenuPanel::Screen::ghost) return 170;
    if (screen == MenuPanel::Screen::credits || screen == MenuPanel::Screen::scores) return 166;
    return 290;
}
void clip_text(Painter& painter, std::size_t start, float top, float bottom) {
    auto& quads = painter.quads;
    auto end = quads.begin() + static_cast<std::ptrdiff_t>(start);
    for (auto it = end; it != quads.end(); ++it) {
        auto q = *it;
        const float y = q.rect[1], height = q.rect[3];
        if (y + height <= top || y >= bottom || height <= 0) continue;
        const float new_top = std::max(y, top), new_bottom = std::min(y + height, bottom);
        const float uv_height = q.uv[3] - q.uv[1];
        q.uv[3] = q.uv[1] + uv_height * ((new_bottom-y)/height);
        q.uv[1] += uv_height * ((new_top-y)/height);
        q.rect[1] = new_top;
        q.rect[3] = new_bottom - new_top;
        *end++ = q;
    }
    quads.erase(end, quads.end());
}
} // namespace

MenuPanel::MenuPanel(Resources& resources) : resources_(resources) {
    inert_x_.fill(canvas_x(690));
    shutter_y_ = canvas_y(100);
    tile1_y_ = -v(resources_.canvas_height());
    arrow1_y_ = v(resources_.canvas_height());
    smoke1_x_ = -v(resources_.canvas_width());
    credits_offset_ = v(credits_min_y[resources_.tier()])*v(font_y_ratio[resources_.tier()]);
    fade_image_ = resources.add("LY_Fade.bmp");
    read_settings(resources.root / "mat.rc4", settings_);
    const auto user_path = settings_path();
    if (!user_path.empty()) read_settings(user_path, settings_);
    ghost_server_selected_ = settings_.ghost == 2;
    for (std::size_t i = 0; i < button_art.size(); ++i) {
        const auto& art = button_art[i];
        button_images_[i] = {resources.add(art.idle), resources.add(art.hover), resources.add(art.pressed)};
    }
    for (const auto name : {"menu_tabanalt.tga", "menu_tabansag.tga", "menu_tvekran.tga",
         "menu_videoekran.tga", "menu_arka_tile1.tga", "menu_arka_tile2.tga", "menu_on_panel.tga",
         "menu_kepeng.tga", "menu_ust_cita.tga", "menu_c4_oklar.tga", "menu_arac_LAZER.tga",
         "mmbtn_exit_yazi.tga", "mmbtn_emin_yazi.tga", "mmbtn_yesno_orta.tga",
         "mmbtn_ayar_yazi.tga", "mmbtn_ses_yazi.tga", "mmbtn_par_yazi.tga",
         "mmbtn_skor_yazi.tga", "mmbtn_ekip_yazi.tga", "mmbtn_ghost_yazi.tga",
         "mmbtn_bos_buton.tga", "mmbtn_renk.tga",
         "loading_ekran1.tga", "load_duman_0.tga", "load_duman_1.tga", "GO_sonuc_fon0.tga",
         "GO_sonuc_fon1.tga", "GO_sonuc_taban.tga", "SM_taban.tga", "ingame_help.tga"}) resources.add(name);
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const auto color = std::string(colors[i]);
        cars_[i] = resources.add("menu_arac_" + color + ".tga");
        robots1_[i] = resources.add("menu_robot_" + color + "1.tga");
        robots2_[i] = resources.add("menu_robot_" + color + "2.tga");
        icons_[i] = resources.add("mmbtn_ikon_" + color + ".tga");
        color_names_[i] = resources.add("mmbtn_yazi_" + color + ".tga");
    }
    for (int i = 0; i <= 10; ++i) {
        volumes_[i] = resources.add("mmbtn_ses_no" + std::to_string(i) + ".tga");
        brightness_[i] = resources.add("mmbtn_par_no" + std::to_string(i) + ".tga");
        if (i) dark_[i] = resources.add("mmbtn_par_nod" + std::to_string(i) + ".tga");
    }
    for (int i = 0; i < 4; ++i)
        ghost_info_images_[i] = resources.add("mmbtn_hayalet" + std::to_string(i) + ".tga");
    for (int i = 0; i < 3; ++i)
        server_images_[i] = resources.add("mmbtn_sunucu" + std::to_string(i) + ".tga");
    time_font_ = resources.add("time_beyazfont_" + std::to_string(resources.tier()) + ".tga", true, true);
    assets::Image white{1, 1, {255,255,255,255}};
    white_ = resources.add_image(std::move(white));
    ghost_blank_image_ = resources.find("mmbtn_bos_buton.tga");
}

void MenuPanel::set_hud_source_panels(std::span<const SourcePanelPickState> panels) {
    hud_source_panels_ = panels; // Root owns the stable six-record live array.
    initialize_source_panels();
}

void MenuPanel::initialize_source_panels() {
    if (source_bounds_initialized_ || resources_.image(fade_image_).width == 0) return;
    auto bitmap = [&](PanelId id, std::uint32_t image, int layer, std::uint32_t rank, std::uint32_t line) {
        auto& panel = native_panels_[panel_index(id)];
        const auto& dimensions = resources_.image(image);
        panel.width = v(dimensions.width); panel.height = v(dimensions.height);
        panel.layer = layer; panel.source_order = source_ui_order(rank,line);
    };
    auto named = [&](PanelId id, std::string_view name, int layer, std::uint32_t rank, std::uint32_t line) {
        bitmap(id,resources_.find(name),layer,rank,line);
    };
    bitmap(PanelId::menu_black,fade_image_,10,2,346);
    named(PanelId::menu_bottom,"menu_tabanalt.tga",11,2,352);
    named(PanelId::menu_right,"menu_tabansag.tga",14,2,359);
    named(PanelId::tv,"menu_tvekran.tga",16,2,366);
    named(PanelId::video,"menu_videoekran.tga",17,2,373);
    named(PanelId::tile1,"menu_arka_tile1.tga",12,2,380);
    named(PanelId::tile2,"menu_arka_tile2.tga",12,2,387);
    named(PanelId::arrow1a,"menu_c4_oklar.tga",15,2,394);
    named(PanelId::arrow1b,"menu_c4_oklar.tga",15,2,402);
    named(PanelId::arrow2a,"menu_c4_oklar.tga",15,2,410);
    named(PanelId::arrow2b,"menu_c4_oklar.tga",15,2,418);
    named(PanelId::front,"menu_on_panel.tga",17,2,426);
    named(PanelId::shutter,"menu_kepeng.tga",14,2,433);
    named(PanelId::top,"menu_ust_cita.tga",16,2,440);
    bitmap(PanelId::car,cars_[settings_.color],18,2,447);
    named(PanelId::laser,"menu_arac_LAZER.tga",19,2,453);
    bitmap(PanelId::robot1,robots1_[settings_.color],19,2,461);
    bitmap(PanelId::robot2,robots2_[settings_.color],19,2,467);
    named(PanelId::loading_bg,"loading_ekran1.tga",100,2,474);
    named(PanelId::smoke1,"load_duman_0.tga",200,2,480);
    named(PanelId::smoke2,"load_duman_1.tga",200,2,487);
    auto& bar = native_panels_[panel_index(PanelId::loading_bar)];
    bar.layer = 999; bar.source_order = source_ui_order(2,2903);
    const auto backdrop = resources_.find(bloom_ ? "GO_sonuc_fon1.tga" : "GO_sonuc_fon0.tga");
    bitmap(PanelId::result_bg,backdrop,2,4,941);
    named(PanelId::result_base,"GO_sonuc_taban.tga",3,4,948);
    bitmap(PanelId::pause_bg,backdrop,12,4,1616);
    named(PanelId::pause_base,"SM_taban.tga",13,4,1624);
    named(PanelId::help,"ingame_help.tga",13,4,1864);
    bitmap(PanelId::curtain,fade_image_,99,0,201);

    auto add_maps = [&](NativePanel& panel, std::array<std::uint32_t,3> images,
                        int x, int y, Action action, int keyboard, HoverCallback hover) {
        if (!panel.active) {
            images[1] = images[2] = images[0];
            action = Action::none; keyboard = -1; hover = HoverCallback::none;
        }
        panel.buttons[panel.button_count++] = {images,x,y,keyboard,action,hover};
    };
    auto add_art = [&](NativePanel& panel, int art, int x, int y, Action action, int keyboard,
                       HoverCallback hover = HoverCallback::menu) {
        auto images = button_images_[art];
        if (art == 9 || art == 10) images[2] = images[1]; // Authored FIRST is 1, not selected map2.
        add_maps(panel,images,x,y,action,keyboard,hover);
    };
    auto passive = [&](NativePanel& panel, std::uint32_t image, int x, int y) {
        add_maps(panel,{image,image,image},x,y,Action::none,-1,HoverCallback::none);
    };
    auto label = [&](NativePanel& panel, std::string_view name, int x, int y) {
        passive(panel,resources_.find(name),x,y);
    };
    constexpr std::array<std::uint32_t,7> inert_lines{501,523,550,576,648,632,608};
    constexpr std::array<std::uint32_t,7> active_lines{512,536,563,592,656,640,620};
    for (std::size_t screen = 0; screen < inert_lines.size(); ++screen) {
        for (std::size_t active = 0; active < 2; ++active) {
            auto& panel = native_panels_[panel_index(PanelId::main_inert)+screen*2+active];
            panel.screen = static_cast<Screen>(screen);
            panel.active = active != 0;
            panel.layer = active ? 18 : 13;
            panel.source_order = source_ui_order(2,(active ? active_lines : inert_lines)[screen]);
            switch (panel.screen) {
            case Screen::main:
                add_art(panel,0,0,0,Action::color,0); add_art(panel,1,0,50,Action::settings,1);
                add_art(panel,2,0,100,Action::scores,2); add_art(panel,3,0,150,Action::credits,3);
                add_art(panel,4,0,200,Action::exit,4); break;
            case Screen::color:
                label(panel,"mmbtn_renk.tga",0,0); add_art(panel,7,0,100,Action::color_left,0);
                passive(panel,icons_[0],97,100); add_art(panel,8,219,100,Action::color_right,1);
                passive(panel,color_names_[0],0,150); add_art(panel,6,0,200,Action::next,2);
                add_art(panel,5,0,300,Action::back,3); break;
            case Screen::ghost:
                label(panel,"mmbtn_ghost_yazi.tga",0,0); passive(panel,ghost_info_images_[0],20,57);
                add_maps(panel,{server_images_[0],server_images_[1],server_images_[1]},
                         0,170,Action::server,-1,HoverCallback::server);
                add_art(panel,9,0,220,Action::local,0,HoverCallback::local);
                add_art(panel,10,0,270,Action::no_ghost,1,HoverCallback::no_ghost);
                add_art(panel,11,0,320,Action::start,2); add_art(panel,5,0,420,Action::back,3); break;
            case Screen::settings:
                label(panel,"mmbtn_ayar_yazi.tga",0,0); label(panel,"mmbtn_ses_yazi.tga",0,100);
                add_art(panel,7,0,150,Action::volume_down,0); passive(panel,volumes_[0],97,150);
                add_art(panel,8,219,150,Action::volume_up,1); label(panel,"mmbtn_par_yazi.tga",0,200);
                add_art(panel,7,0,250,Action::brightness_down,2); passive(panel,brightness_[0],97,250);
                add_art(panel,8,219,250,Action::brightness_up,3); add_art(panel,5,0,300,Action::back,4); break;
            case Screen::exit:
                label(panel,"mmbtn_exit_yazi.tga",0,0); label(panel,"mmbtn_emin_yazi.tga",0,100);
                add_art(panel,12,0,150,Action::quit,0); label(panel,"mmbtn_yesno_orta.tga",138,150);
                add_art(panel,13,178,150,Action::back,1); add_art(panel,5,0,300,Action::back,2); break;
            case Screen::credits:
                label(panel,"mmbtn_ekip_yazi.tga",0,0); add_art(panel,5,0,424,Action::back,0); break;
            case Screen::scores:
                label(panel,"mmbtn_skor_yazi.tga",0,0); add_art(panel,5,0,424,Action::back,0); break;
            default: break;
            }
        }
    }
    auto action_panel = [&](PanelId id, Screen screen, Screen origin, int layer, std::uint32_t line) -> NativePanel& {
        auto& panel = native_panels_[panel_index(id)];
        panel.screen = screen; panel.return_screen = origin; panel.active = true;
        panel.layer = layer; panel.source_order = source_ui_order(4,line);
        return panel;
    };
    auto& result = action_panel(PanelId::result_buttons,Screen::results,Screen::results,5,964);
    add_art(result,15,0,0,Action::restart,0,HoverCallback::gameplay);
    add_art(result,16,155,0,Action::menu,1,HoverCallback::gameplay);
    auto& pause = action_panel(PanelId::pause_buttons,Screen::pause,Screen::pause,15,1631);
    add_art(pause,14,0,0,Action::resume,0,HoverCallback::gameplay);
    add_art(pause,15,154,0,Action::restart,1,HoverCallback::gameplay);
    add_art(pause,16,308,0,Action::menu,2,HoverCallback::gameplay);
    for (const auto id : {PanelId::result_restart,PanelId::result_menu,PanelId::pause_restart,PanelId::pause_menu}) {
        const bool result_origin = id == PanelId::result_restart || id == PanelId::result_menu;
        const bool restart = id == PanelId::result_restart || id == PanelId::pause_restart;
        auto& panel = action_panel(id,restart ? Screen::confirm_restart : Screen::confirm_menu,
                                  result_origin ? Screen::results : Screen::pause,15,
                                  result_origin ? (restart ? 987 : 995) : (restart ? 1640 : 1648));
        add_art(panel,17,0,0,Action::yes,0,HoverCallback::gameplay);
        add_art(panel,18,155,0,Action::no,1,HoverCallback::gameplay);
    }
    // PANEL bounds are cached from the authored first map, before any pan_setbmap replacements.
    for (auto& panel : native_panels_) {
        for (std::size_t i = 0; i < panel.button_count; ++i) {
            const auto& button = panel.buttons[i];
            const auto& first = resources_.image(button.images[2]);
            panel.width = var_max(panel.width,v(button.x)+v(first.width));
            panel.height = var_max(panel.height,v(button.y)+v(first.height));
        }
    }
    source_bounds_initialized_ = true;
}

MenuPanel::PanelGeometry MenuPanel::source_panel(std::size_t index) const noexcept {
    const auto& panel = native_panels_[index];
    PanelGeometry geometry{v(0),v(0),v(resources_.scale_x()),v(resources_.scale_y()),panel.width,panel.height,
                           panel.layer,panel.source_order,false};
    if (index >= panel_index(PanelId::hud0)) {
        const auto hud = index-panel_index(PanelId::hud0);
        if (hud < hud_source_panels_.size()) {
            const auto& source = hud_source_panels_[hud];
            geometry = {source.x,source.y,source.sx,source.sy,v(source.width),v(source.height),
                        source.layer,source.source_order,source.visible};
        }
        return geometry;
    }
    const auto id = static_cast<PanelId>(index);
    if (index < panel_index(PanelId::main_inert))
        geometry.visible = menu_art_visible_ && (menu_visible() || screen_ == Screen::loading);
    if (index >= panel_index(PanelId::main_inert) && index <= panel_index(PanelId::exit_active)) {
        geometry.x = panel.active ? canvas_x(354) : inert_x_[static_cast<std::size_t>(panel.screen)];
        geometry.y = canvas_y(origin_y(panel.screen));
        if (panel.active) geometry.visible = ready() && screen_ == panel.screen;
        else geometry.visible = (menu_visible() && screen_ == panel.screen && !ready()
            && !incoming_visible_ && transition_ != Transition::gap
            && transition_ != Transition::quit_wait && transition_ != Transition::quit_black_wait)
            || (incoming_visible_ && target_ == panel.screen);
        return geometry;
    }
    const bool confirmation = screen_ == Screen::confirm_restart || screen_ == Screen::confirm_menu;
    const bool result = screen_ == Screen::results || (confirmation && confirmation_return_ == Screen::results);
    const bool pause = screen_ == Screen::pause || (confirmation && confirmation_return_ == Screen::pause);
    switch (id) {
    case PanelId::menu_black:
        geometry.sx = v(resources_.canvas_width()/30); geometry.sy = v(resources_.canvas_height()/7); break;
    case PanelId::menu_right: case PanelId::tv:
        geometry.x = v(resources_.canvas_width()-1); geometry.sx = -v(resources_.scale_x()); break;
    case PanelId::video:
        geometry.x = canvas_x(750); geometry.y = canvas_y(37);
        geometry.sx += v(video_adjust[resources_.tier()]);
        geometry.sy += v(video_adjust[resources_.tier()]); break;
    case PanelId::tile1: case PanelId::tile2:
        geometry.x = canvas_x(341); geometry.y = id == PanelId::tile1 ? tile1_y_ : tile_y_;
        geometry.sx += v(tile_x_adjust[resources_.tier()]);
        geometry.sy += v(tile_y_adjust[resources_.tier()]); break;
    case PanelId::arrow1a: case PanelId::arrow1b: case PanelId::arrow2a: case PanelId::arrow2b:
        geometry.y = id == PanelId::arrow1a || id == PanelId::arrow2a ? arrow_y_ : arrow1_y_;
        geometry.sy += v(tile_y_adjust[resources_.tier()]);
        if (id == PanelId::arrow2a || id == PanelId::arrow2b) {
            geometry.x = v(resources_.canvas_width()-1); geometry.sx = -v(resources_.scale_x());
        }
        break;
    case PanelId::front:
        geometry.x = canvas_x(320); geometry.sx += v(tile_x_adjust[resources_.tier()]); break;
    case PanelId::shutter:
        geometry.x = canvas_x(341); geometry.y = shutter_y_; geometry.sx += v(tile_x_adjust[resources_.tier()]); break;
    case PanelId::car: case PanelId::laser:
        geometry.x = v(resources_.canvas_width()-1); geometry.y = v(resources_.canvas_height()-1);
        geometry.sx = -geometry.sx; geometry.sy = -geometry.sy; break;
    case PanelId::robot2: geometry.x = canvas_x(350); break;
    case PanelId::loading_bg: geometry.visible = screen_ == Screen::loading; break;
    case PanelId::smoke1: case PanelId::smoke2:
        geometry.x = id == PanelId::smoke1 ? smoke1_x_ : smoke_x_;
        geometry.y = canvas_y(426); geometry.visible = screen_ == Screen::loading; break;
    case PanelId::loading_bar:
        geometry.y = canvas_y(734); geometry.sx = geometry.sy = v(1);
        geometry.width = loading_width_; geometry.height = canvas_y(10);
        geometry.visible = loading_bar_visible_; break;
    case PanelId::result_bg:
        geometry.visible = result_backdrop_visible_ && transition_ != Transition::menu_return_wait; break;
    case PanelId::result_base:
        geometry.x = canvas_x(136); geometry.y = canvas_y(294);
        geometry.visible = result && (screen_ != Screen::results || result_buttons_); break;
    case PanelId::pause_bg: geometry.visible = pause; break;
    case PanelId::pause_base:
        geometry.x = canvas_x(136); geometry.y = canvas_y(294); geometry.visible = pause; break;
    case PanelId::result_buttons: case PanelId::result_restart: case PanelId::result_menu:
    case PanelId::pause_buttons: case PanelId::pause_restart: case PanelId::pause_menu:
        geometry.x = canvas_x(id == PanelId::pause_buttons ? 288 : 365); geometry.y = canvas_y(436);
        geometry.visible = ready() && screen_ == panel.screen
            && (screen_ == Screen::pause || screen_ == Screen::results
                || confirmation_return_ == panel.return_screen);
        break;
    case PanelId::help: geometry.visible = screen_ == Screen::help; break;
    case PanelId::curtain:
        geometry.sx = v(resources_.canvas_width()/30); geometry.sy = v(resources_.canvas_height()/7);
        if (root_source_curtain_) {
            const auto& root = *root_source_curtain_;
            geometry = {root.x,root.y,root.sx,root.sy,v(root.width),v(root.height),
                        root.layer,root.source_order,root.visible};
        }
        geometry.visible = geometry.visible || owns_source_curtain();
        break;
    default: break;
    }
    return geometry;
}

std::uint32_t MenuPanel::source_button_image(std::size_t index, std::size_t button, std::size_t slot) const noexcept {
    const auto& panel = native_panels_[index];
    if (panel.screen == Screen::color) {
        if (button == 2) return icons_[settings_.color];
        if (button == 4) return color_names_[settings_.color];
    } else if (panel.screen == Screen::settings) {
        if (button == 3) return volumes_[settings_.volume/10];
        if (button == 7) return settings_.brightness/5 < 0
            ? dark_[-settings_.brightness/5] : brightness_[settings_.brightness/5];
    } else if (panel.screen == Screen::ghost) {
        if (button == 1) return ghost_info_images_[panel.active ? ghost_info_ : 0];
        if (button == 2) return ghost_server_selected_ ? server_images_[2] : ghost_blank_image_;
        if (button == 3 && settings_.ghost == 1) return button_images_[19][0];
        if (button == 4 && settings_.ghost == 0) return button_images_[20][0];
    }
    return panel.buttons[button].images[slot];
}

int MenuPanel::select_source_panel(const MenuInput& input) const noexcept {
    if (!input.pointer_enabled || !source_bounds_initialized_) return -1;
    const Var x = var_clamp(physics::property(input.x),v(0),v(resources_.canvas_width()-1));
    const Var y = var_clamp(physics::property(input.y),v(0),v(resources_.canvas_height()-1));
    int selected = -1, layer{};
    std::uint32_t order{};
    for (std::size_t i = 0; i < panel_count; ++i) {
        const auto panel = source_panel(i);
        if ((!panel.visible && !previously_drawn_[i]) || x < panel.x || y < panel.y
            || x > panel.x+panel.width*panel.sx || y > panel.y+panel.height*panel.sy) continue;
        // Last eligible source PANEL wins: layer ASC, source order DESC.
        if (selected < 0 || panel.layer > layer || (panel.layer == layer && panel.source_order < order)) {
            selected = static_cast<int>(i); layer = panel.layer; order = panel.source_order;
        }
    }
    return selected;
}

int MenuPanel::hit_native_button(std::size_t index, const MenuInput& input) const noexcept {
    if (!input.pointer_enabled || !input.pointer_inside) return -1;
    const auto panel = source_panel(index);
    const Var x = var_clamp(physics::property(input.x),v(0),v(resources_.canvas_width()-1));
    const Var y = var_clamp(physics::property(input.y),v(0),v(resources_.canvas_height()-1));
    const auto local_x = (x-panel.x).raw >> 10, local_y = (y-panel.y).raw >> 10;
    const auto& buttons = native_panels_[index];
    for (std::size_t i = 0; i < buttons.button_count; ++i) {
        const auto& button = buttons.buttons[i];
        const auto& first = resources_.image(source_button_image(index,i,2));
        const auto left = (v(button.x)*panel.sx).raw >> 10, top = (v(button.y)*panel.sy).raw >> 10;
        const auto width = (v(first.width)*panel.sx).raw >> 10, height = (v(first.height)*panel.sy).raw >> 10;
        if (local_x >= left && local_y >= top && local_x <= left+width && local_y <= top+height)
            return static_cast<int>(i); // A NULL-callback child still consumes the first hit.
    }
    return -1;
}

MenuPanel::PointerTarget MenuPanel::source_pointer_target(const MenuInput& input) const noexcept {
    if (!input.pointer_enabled) return {};
    const int current = select_source_panel(input);
    if (current >= 0) {
        const int hit = hit_native_button(static_cast<std::size_t>(current),input);
        if (hit >= 0) return {current,hit};
    }
    // Source previous helper deliberately bypasses PANEL bbox, visibility and layer checks.
    if (current_panel_ >= 0 && current_panel_ != current) {
        const int hit = hit_native_button(static_cast<std::size_t>(current_panel_),input);
        if (hit >= 0) return {current_panel_,hit};
    }
    return {};
}

void MenuPanel::hover_callback(HoverCallback callback) {
    switch (callback) {
    case HoverCallback::menu: cue("ses_buton_1"); break;
    case HoverCallback::gameplay: cue("buton_0_wav"); break;
    case HoverCallback::local: ghost_info_ = 2; cue("ses_buton_1"); break;
    case HoverCallback::no_ghost: ghost_info_ = 3; cue("ses_buton_1"); break;
    case HoverCallback::server: break; // Original bound callback's offline guard returns before effects.
    case HoverCallback::none: break;
    }
}

MenuPanel::PointerDispatch MenuPanel::dispatch_source_panel(std::size_t index, const MenuInput& input, int transition) {
    const int hit = hit_native_button(index,input);
    const auto& panel = native_panels_[index];
    auto& states = button_transients_[index];
    for (std::size_t i = 0; i < panel.button_count; ++i) {
        if (static_cast<int>(i) != hit) { states[i] = 0; continue; } // Every SECOND callback is NULL.
        const auto& button = panel.buttons[i];
        if (index == active_panel(screen_) && button.keyboard_index >= 0) hover_ = button.keyboard_index;
        PointerDispatch result{game::UiCommand::none,true,false};
        if (transition == 1) {
            if (button.action != Action::none) {
                if (index == active_panel(screen_) && button.keyboard_index >= 0) focus_ = button.keyboard_index;
                result.first_callback = true;
                result.command = activate(button.action,panel.screen,panel.return_screen);
            }
            states[i] = 2; // FIRST callback effects precede this write to the original source node.
        } else if (transition == 2) states[i] = -2;
        else if (input.pointer_down) states[i] = 2;
        else {
            if (states[i] == 0) hover_callback(button.hover);
            states[i] = -3;
        }
        return result;
    }
    return {};
}

bool MenuPanel::menu_visible() const noexcept { return screen_ <= Screen::exit; }
bool MenuPanel::ready() const noexcept {
    return transition_ == Transition::idle && screen_ != Screen::hidden && screen_ != Screen::loading
        && (screen_ != Screen::results || result_buttons_);
}
bool MenuPanel::cursor_visible() const noexcept {
    if (menu_visible()) return transition_ != Transition::startup_wait;
    if (!ready()) return false;
    return screen_ == Screen::pause || screen_ == Screen::results ||
        screen_ == Screen::confirm_restart || screen_ == Screen::confirm_menu;
}
bool MenuPanel::owns_source_curtain() const noexcept {
    if (screen_ == Screen::loading || transition_ == Transition::menu_return_wait) return true;
    if (menu_visible()) return fade_ > v(0);
    return transition_ == Transition::command_fade || transition_ == Transition::command_wait
        || transition_ == Transition::result_restart_wait;
}
std::string_view MenuPanel::take_sound() noexcept {
    if (!sound_count_) return {};
    const auto result = sounds_[sound_begin_];
    sound_begin_ = (sound_begin_+1)%sounds_.size(); --sound_count_;
    return result;
}
bool MenuPanel::take_graphics_open_request() noexcept {
    const bool request = graphics_open_request_;
    graphics_open_request_ = false;
    return request;
}
bool MenuPanel::take_video_stop_request() noexcept {
    const bool request = video_stop_request_;
    video_stop_request_ = false;
    return request;
}
bool MenuPanel::take_music_stop_request() noexcept {
    const bool request = music_stop_request_;
    music_stop_request_ = false;
    return request;
}
bool MenuPanel::take_menu_return_view_flags() noexcept {
    const bool request = menu_return_view_flags_;
    menu_return_view_flags_ = false;
    return request;
}
bool MenuPanel::take_results_hud_restore() noexcept {
    const bool request = results_hud_restore_;
    results_hud_restore_ = false;
    return request;
}
game::UiCommand MenuPanel::take_continuation_command() noexcept {
    const auto command = pending_;
    pending_ = game::UiCommand::none;
    return command;
}
std::optional<Var> MenuPanel::take_mouse_mode_write() noexcept {
    const auto mode = mouse_mode_write_;
    mouse_mode_write_.reset();
    return mode;
}
void MenuPanel::set_selected_ghost(int preference) {
    if (preference < 0 || preference > 2) throw std::out_of_range("Ghost preference must be 0, 1, or 2");
    settings_.ghost = preference;
    ghost_server_selected_ = preference == 2;
}
void MenuPanel::cue(std::string_view name) {
    if (sound_count_ == sounds_.size())
        throw std::logic_error("Native UI feedback must be drained after each input/update");
    sounds_[(sound_begin_+sound_count_)%sounds_.size()] = name;
    ++sound_count_;
}
void MenuPanel::reset_main(bool first) {
    screen_ = target_ = Screen::main;
    first_ = first;
    inert_x_.fill(canvas_x(690));
    menu_art_visible_ = true;
    loading_bar_visible_ = false;
    ghost_server_selected_ = settings_.ghost == 2;
    fade_ = v(100); shutter_y_ = canvas_y(first ? 100 : -434);
    fade_progress_ = v(100); timer_ = v(0);
    tile_y_ = arrow_y_ = laser_time_ = laser_alpha_ = v(0);
    tile1_y_ = -v(resources_.canvas_height()); arrow1_y_ = v(resources_.canvas_height());
    smoke1_x_ = -v(resources_.canvas_width()); smoke_x_ = v(0);
    transition_ = Transition::startup_wait;
    deadline_ = seconds_ + .5;
    focus_ = hover_ = -1;
    incoming_visible_ = false;
}
void MenuPanel::transition(Screen target, game::UiCommand command) {
    target_ = target; command_ = command;
    transition_ = Transition::outgoing_nudge;
    timer_ = v(0);
    focus_ = hover_ = -1;
    incoming_visible_ = false;
    cue("ses_menu_carp");
    advance(current_ticks_,seconds_); // Direct callback entry executes through its first source wait.
}

void MenuPanel::advance(Var ticks, double seconds) {
    // Source loops execute the next loop on a clamp/break frame, but wait(1) on non-clamp frames.
    for (;;) {
        auto move = [&](double target_coordinate, double duration, bool increasing) {
            auto& x = inert_x_[static_cast<std::size_t>(incoming_visible_ ? target_ : screen_)];
            const Var target = canvas_x(target_coordinate);
            const Var step = v(6.25) / v(duration) * ticks * v(resources_.scale_x());
            if (increasing) {
                x += step;
                if (x < target) return false;
            } else {
                x -= step;
                if (x > target) return false;
            }
            x = target;
            return true;
        };
        switch (transition_) {
        case Transition::startup_wait:
            if (seconds < deadline_) return;
            mouse_mode_write_ = v(2); // ana_menu:1037, after the half-second video-start wait.
            transition_ = Transition::fade;
            break;
        case Transition::fade:
            fade_ = fade_progress_;
            fade_progress_ -= v(6.25)*ticks;
            if (fade_progress_ > v(0)) return;
            fade_ = v(0); timer_ = v(0);
            transition_ = first_ ? Transition::shutter_wait : Transition::entrance_wait;
            if (!first_) cue("ses_menu_carp");
            break;
        case Transition::shutter_wait:
            timer_ += v(6.25)/v(.5)*ticks;
            if (timer_ <= v(100)) return;
            transition_ = Transition::shutter;
            break;
        case Transition::shutter:
            shutter_y_ -= v(6.25)/v(.25)*ticks*v(resources_.scale_y());
            if (shutter_y_ >= canvas_y(-434)) return;
            shutter_y_ = canvas_y(-434); timer_ = v(0);
            transition_ = Transition::entrance_wait;
            cue("ses_menu_carp");
            break;
        case Transition::entrance_wait:
            if (timer_ <= v(100)) { timer_ += v(6.25)/v(.25)*ticks; return; }
            incoming_visible_ = true;
            transition_ = Transition::incoming;
            break;
        case Transition::outgoing_nudge:
            if (!move(334,.5,false)) return;
            transition_ = Transition::outgoing;
            break;
        case Transition::outgoing:
            if (!move(690,.08,true)) return;
            if (command_ == game::UiCommand::quit) {
                transition_ = Transition::quit_wait; // Exit inert panel off, then wait(1).
                return;
            }
            if (command_ == game::UiCommand::play) {
                smoke1_x_ = -v(resources_.canvas_width());
                screen_ = Screen::loading;
                fade_ = v(100);
                smoke_x_ = v(0); loading_width_ = v(1); loading_bar_visible_ = false;
                music_stop_request_ = true; // ana_menu:3089-3091, explicit load-entry stop before wait(1).
                transition_ = Transition::loading_initial_wait; // Loading art first, then wait(1).
                return;
            }
            transition_ = Transition::gap; timer_ = v(0);
            break;
        case Transition::gap:
            if (timer_ <= v(100)) { timer_ += v(6.25)/v(.05)*ticks; return; }
            incoming_visible_ = true;
            transition_ = Transition::incoming;
            break;
        case Transition::incoming:
            if (!move(354,.1,false)) return;
            transition_ = Transition::bounce1;
            break;
        case Transition::bounce1:
            if (!move(374,.2,true)) return;
            transition_ = Transition::bounce2;
            break;
        case Transition::bounce2:
            if (!move(354,.3,false)) return;
            transition_ = Transition::bounce3;
            break;
        case Transition::bounce3:
            if (!move(364,.4,true)) return;
            transition_ = Transition::bounce4;
            break;
        case Transition::bounce4:
            if (!move(354,.5,false)) return;
            screen_ = target_; transition_ = Transition::idle; incoming_visible_ = false;
            credits_offset_ = v(credits_min_y[resources_.tier()])*v(font_y_ratio[resources_.tier()]);
            return;
        case Transition::command_fade:
            if (fade_progress_ <= v(100)) {
                fade_ = fade_progress_; fade_progress_ += v(6.25)/v(.25)*ticks; return;
            }
            fade_ = v(100);
            music_stop_request_ = true; // Both Restart/Menu callbacks stop after fade, before negative wait.
            transition_ = Transition::command_wait;
            deadline_ = seconds + (command_ == game::UiCommand::restart ? 1 : .5);
            return;
        case Transition::command_wait:
            if (seconds < deadline_) return;
            if (confirmation_return_ == Screen::results)
                results_hud_restore_ = true; // oyunici_panel_on precedes both results return tails.
            if (command_ == game::UiCommand::menu) {
                screen_ = Screen::hidden;
                menu_return_view_flags_ = true; // Both views NOSHADER/NOPARTICLE on, then wait(1).
                transition_ = Transition::menu_return_wait;
                return;
            }
            if (confirmation_return_ == Screen::results) {
                transition_ = Transition::result_restart_wait; // Result restart has its own extra wait(1).
                return;
            }
            pending_ = command_; command_ = game::UiCommand::none;
            screen_ = Screen::hidden; transition_ = Transition::idle;
            return;
        case Transition::menu_return_wait:
        case Transition::result_restart_wait:
            pending_ = command_; command_ = game::UiCommand::none;
            screen_ = Screen::hidden; transition_ = Transition::idle;
            return;
        case Transition::quit_wait:
            fade_ = v(100);
            video_stop_request_ = true; // media_stop(0), then the second explicit wait(1).
            transition_ = Transition::quit_black_wait;
            return;
        case Transition::quit_black_wait:
            pending_ = command_; command_ = game::UiCommand::none;
            screen_ = Screen::hidden; transition_ = Transition::idle;
            return;
        case Transition::loading_initial_wait:
            mouse_mode_write_ = v(0);
            video_stop_request_ = true;
            loading_wait_frames_ = 2;
            transition_ = Transition::loading_media_wait;
            return;
        case Transition::loading_media_wait:
            if (--loading_wait_frames_ > 0) return;
            menu_art_visible_ = false;
            transition_ = Transition::loading_hide_wait;
            return;
        case Transition::loading_hide_wait:
            // ponytail: Retain hidden immutable menu resources; evict if resident-memory demand matters.
            transition_ = Transition::loading_purge_wait;
            return;
        case Transition::loading_purge_wait:
            persist_settings();
            transition_ = Transition::loading_save_wait;
            return;
        case Transition::loading_save_wait:
            pending_ = command_; command_ = game::UiCommand::none;
            transition_ = Transition::idle;
            return;
        case Transition::idle: return;
        }
    }
}

void MenuPanel::synchronize(const game::SharedState& state, game::FrameTiming timing) {
    seconds_ = timing.seconds_deadlines;
    current_ticks_ = var_max(v(0),timing.originaltime_step_ticks);
    loading_bar_visible_ = state.loading_visible;
    if (deadline_ == 0 && transition_ == Transition::startup_wait) deadline_ = seconds_ + .5;
    bloom_ = static_cast<bool>(state.bloom_aktif);
    initialize_source_panels();
    result_buttons_ = state.finish_buttons_visible;
    if (record_error_ != state.record_error) record_error_ = state.record_error;
    result_backdrop_visible_ = state.game_over_ok == v(1) && state.gameover_panel_acik == v(1) && state.black_visible;
    result_backdrop_alpha_ = state.black_alpha;
    if (state.help_visible && screen_ != Screen::help) {
        screen_ = Screen::help; transition_ = Transition::idle;
        focus_ = hover_ = -1;
    } else if (!state.help_visible && screen_ == Screen::help) screen_ = Screen::hidden;
    if (!state.help_visible && !record_error_.empty() && pending_ == game::UiCommand::none
        && transition_ == Transition::idle) {
        if (screen_ == Screen::hidden && state.phase == game::GamePhase::paused) {
            screen_ = Screen::pause;
            mouse_mode_write_ = v(2); // Native failed-command recovery restores pointer control once.
        } else if (screen_ == Screen::hidden && state.phase == game::GamePhase::results) {
            screen_ = Screen::results;
            mouse_mode_write_ = v(2);
        } else if (screen_ == Screen::loading && state.phase == game::GamePhase::menu && !state.loading_visible) {
            screen_ = Screen::ghost;
            inert_x_[static_cast<std::size_t>(Screen::ghost)] = canvas_x(354);
            menu_art_visible_ = true;
            fade_ = v(0);
            mouse_mode_write_ = v(2);
        }
    }
    if (!state.help_visible && state.phase != phase_) {
        if (state.phase == game::GamePhase::menu) reset_main(false);
        else if (state.phase == game::GamePhase::paused) {
            screen_ = Screen::pause; transition_ = Transition::idle;
        } else if (state.phase == game::GamePhase::results) {
            screen_ = Screen::results; transition_ = Transition::idle;
            // The original result clips the six-byte "ZAMAN " prefix from the frozen HUD clock.
            result_text_ = "Yarışı tamamlama süreniz:  ";
            result_text_ += std::string_view(state.timepanel_time_str).substr(
                std::min<std::size_t>(6,state.timepanel_time_str.size()));
        } else if (screen_ != Screen::loading) {
            screen_ = Screen::hidden; transition_ = Transition::idle;
        }
        focus_ = hover_ = -1;
    }
    phase_ = state.phase;
    if (state.loading_visible) {
        screen_ = Screen::loading;
        const Var progress = var_clamp(state.loading_progress,v(0),v(100));
        loading_width_ = state.loading_stage == 0 ? v(1) + v(8.23)*progress*v(resources_.scale_x())
            : state.loading_stage == 4 ? (state.loading_progress > v(100)
                ? v(1024)*v(resources_.scale_x())
                : v(1) + v(10.23)*progress*v(resources_.scale_x()))
            : state.loading_stage == 1 ? (v(824) + progress)*v(resources_.scale_x())
            : (v(924) + var_clamp(state.oyun_oynanabilir,v(0),v(100)))*v(resources_.scale_x());
    } else if (screen_ == Screen::loading && state.phase != game::GamePhase::menu
        && transition_ == Transition::idle) {
        screen_ = Screen::hidden;
    }
}

void MenuPanel::update(const game::SharedState& state, game::FrameTiming timing) {
    synchronize(state,timing);
    const Var ticks = current_ticks_;
    advance(ticks, seconds_);
    if (menu_visible() && transition_ != Transition::startup_wait) {
        const Var delta = v(6.25)/v(.4)*ticks*v(resources_.scale_y());
        const Var height = v(resources_.canvas_height());
        tile_y_ += delta;
        if (tile_y_ > height) tile_y_ = -height + var_modulo(var_abs(tile_y_),height);
        tile1_y_ += delta;
        if (tile1_y_ > height) tile1_y_ = -height + var_modulo(var_abs(tile1_y_),height);
        arrow_y_ -= v(.8)*delta;
        if (arrow_y_ < -height) arrow_y_ = height - var_modulo(var_abs(arrow_y_),height);
        arrow1_y_ -= v(.8)*delta;
        if (arrow1_y_ < -height) arrow1_y_ = height - var_modulo(var_abs(arrow1_y_),height);
        laser_time_ += v(6.25)*ticks;
        laser_alpha_ = laser_time_ <= v(25) ? laser_time_ : laser_time_ <= v(50) ? v(50)-laser_time_
            : laser_time_ <= v(75) ? laser_time_-v(50) : laser_time_ <= v(175) ? (v(175)-laser_time_)/v(4) : v(0);
        laser_time_ = var_modulo(laser_time_,v(800));
        if (screen_ == Screen::credits && ready()) {
            credits_offset_ += v(1.6)*v(font_y_ratio[resources_.tier()])*ticks;
            if (credits_offset_ > v(646)*v(font_y_ratio[resources_.tier()]))
                credits_offset_ = v(credits_min_y[resources_.tier()])*v(font_y_ratio[resources_.tier()]);
        }
        if (screen_ == Screen::ghost && ready() && ghost_info_ != 0
            && (pointer_cursor_x_ < canvas_x(354) || pointer_cursor_x_ > canvas_x(684)
                || pointer_cursor_y_ < canvas_y(390) || pointer_cursor_y_ > canvas_y(484)))
            ghost_info_ = 0; // Source menu continuation, not an unconditional per-pass hover rewrite.
    }
    if (state.loading_visible) {
        const Var delta = v(6.25)/v(state.loading_stage == 0 || state.loading_stage == 4 ? .4 : 1)*ticks*v(resources_.scale_x());
        const Var width = v(resources_.canvas_width());
        smoke_x_ += delta;
        if (smoke_x_ > width) smoke_x_ = -width + var_modulo(var_abs(smoke_x_),width);
        smoke1_x_ += delta;
        if (smoke1_x_ > width) smoke1_x_ = -width + var_modulo(var_abs(smoke1_x_),width);
    }
}

void MenuPanel::build_buttons(Screen screen, Var x) {
    buttons_screen_ = screen;
    button_count_ = 0;
    const Var y = canvas_y(origin_y(screen));
    auto add = [&](int art, double bx, double by, Action action, bool absolute = false) {
        const auto& ids = button_images_[art];
        buttons_[button_count_++] = {ids[0],ids[1],ids[2], pixel((absolute ? v(0) : x)+canvas_x(bx)),
            pixel((absolute ? v(0) : y)+canvas_y(by)), action};
    };
    auto native_graphics = [&](float bx, float by, float width, float height) {
        buttons_[button_count_++] = {white_,white_,white_,bx,by,Action::graphics,width,height};
    };
    switch (screen) {
    case Screen::main:
        add(0,0,0,Action::color); add(1,0,50,Action::settings); add(2,0,100,Action::scores);
        add(3,0,150,Action::credits); add(4,0,200,Action::exit); break;
    case Screen::color:
        add(7,0,100,Action::color_left); add(8,219,100,Action::color_right);
        add(6,0,200,Action::next); add(5,0,300,Action::back); break;
    case Screen::ghost:
        add(settings_.ghost == 1 ? 19 : 9,0,220,Action::local);
        add(settings_.ghost == 0 ? 20 : 10,0,270,Action::no_ghost);
        add(11,0,320,Action::start); add(5,0,420,Action::back); break;
    case Screen::settings:
        add(7,0,150,Action::volume_down); add(8,219,150,Action::volume_up);
        add(7,0,250,Action::brightness_down); add(8,219,250,Action::brightness_up);
        add(5,0,300,Action::back);
        native_graphics(pixel(x),pixel(canvas_y(339.2)),pixel(canvas_x(330)),pixel(canvas_y(35.2)));
        break;
    case Screen::exit:
        add(12,0,150,Action::quit); add(13,178,150,Action::back); add(5,0,300,Action::back); break;
    case Screen::credits: case Screen::scores: add(5,0,424,Action::back); break;
    case Screen::pause:
        add(14,288,436,Action::resume,true); add(15,442,436,Action::restart,true);
        add(16,596,436,Action::menu,true);
        native_graphics(pixel(canvas_x(376)),pixel(canvas_y(392)),pixel(canvas_x(272)),pixel(canvas_y(32)));
        break;
    case Screen::results:
        if (result_buttons_) { add(15,365,436,Action::restart,true); add(16,520,436,Action::menu,true); }
        break;
    case Screen::confirm_restart: case Screen::confirm_menu:
        add(17,365,436,Action::yes,true); add(18,520,436,Action::no,true); break;
    default: break;
    }
}

std::size_t MenuPanel::active_panel(Screen screen) const noexcept {
    if (screen <= Screen::exit) return panel_index(PanelId::main_active)+static_cast<std::size_t>(screen)*2;
    if (screen == Screen::pause) return panel_index(PanelId::pause_buttons);
    if (screen == Screen::results) return panel_index(PanelId::result_buttons);
    if (screen == Screen::confirm_restart)
        return panel_index(confirmation_return_ == Screen::results ? PanelId::result_restart : PanelId::pause_restart);
    if (screen == Screen::confirm_menu)
        return panel_index(confirmation_return_ == Screen::results ? PanelId::result_menu : PanelId::pause_menu);
    return panel_count;
}

int MenuPanel::hit_graphics(const MenuInput& input) const noexcept {
    if (!input.pointer_enabled || !input.pointer_inside || !ready() || buttons_screen_ != screen_) return -1;
    for (std::size_t i = 0; i < button_count_; ++i) {
        const auto& button = buttons_[i];
        if (button.action == Action::graphics && input.x >= button.x && input.y >= button.y
            && input.x <= button.x+button.native_width && input.y <= button.y+button.native_height)
            return static_cast<int>(i); // Native extension uses its authored float rect, never a source bbox.
    }
    return -1;
}

bool MenuPanel::would_open_graphics(const MenuInput& input) const noexcept {
    if (pending_ != game::UiCommand::none || !ready()
        || (screen_ != Screen::settings && screen_ != Screen::pause)
        || buttons_screen_ != screen_) return false;
    if (input.pointer_enabled && !previous_ui_down_ && input.pointer_pressed) {
        const auto source = source_pointer_target(input);
        if (source.button >= 0
            && native_panels_[source.panel].buttons[source.button].action != Action::none) return false;
        if (hit_graphics(input) >= 0) return true;
    }
    int focus = focus_;
    if (input.tab && button_count_)
        focus = input.shift ? (focus <= 0 ? static_cast<int>(button_count_)-1 : focus-1)
            : (focus+1) % static_cast<int>(button_count_);
    return input.enter && focus >= 0 && static_cast<std::size_t>(focus) < button_count_
        && buttons_[focus].action == Action::graphics;
}

void MenuPanel::finish_ui_pass(UiPass pass) noexcept {
    if (pass != UiPass::AfterWorld) return;
    const auto keyboard_panel = active_panel(screen_);
    const bool graphics_visible = ready() && (screen_ == Screen::settings || screen_ == Screen::pause);
    if (graphics_visible) {
        if (!previously_drawn_[keyboard_panel]) graphics_transient_ = 0;
        rendered_graphics_transient_ = graphics_transient_;
        if (graphics_transient_ > 0) --graphics_transient_;
        else if (graphics_transient_ < 0) ++graphics_transient_;
    }
    for (std::size_t i = 0; i < panel_count; ++i) {
        const bool drawn = source_panel(i).visible;
        if (drawn) {
            auto& states = button_transients_[i];
            auto& rendered = rendered_button_transients_[i];
            for (std::size_t button = 0; button < native_panels_[i].button_count; ++button) {
                if (!previously_drawn_[i]) states[button] = 0;
                rendered[button] = states[button]; // Renderer selects from the pre-decay sign.
                if (states[button] > 0) --states[button];
                else if (states[button] < 0) ++states[button];
            }
        }
        previously_drawn_[i] = drawn;
    }
}

game::UiCommand MenuPanel::activate(Action action, Screen origin, Screen return_screen) {
    using game::UiCommand;
    switch (action) {
    case Action::none: case Action::server: break; // Bound server FIRST is guarded off in the offline source flow.
    case Action::color: screen_ = origin; transition(Screen::color); break;
    case Action::settings: screen_ = origin; transition(Screen::settings); break;
    case Action::scores: screen_ = origin; transition(Screen::scores); break;
    case Action::credits: screen_ = origin; transition(Screen::credits); break;
    case Action::exit: screen_ = origin; transition(Screen::exit); break;
    case Action::back: screen_ = origin; transition(origin == Screen::ghost ? Screen::color : Screen::main); break;
    case Action::next: screen_ = origin; transition(Screen::ghost); break;
    case Action::color_left: case Action::color_right:
        settings_.color = (settings_.color + (action == Action::color_left ? 9 : 1)) % 10;
        cue("ses_buton_3"); break;
    case Action::volume_down: case Action::volume_up:
        settings_.volume = std::clamp(settings_.volume + (action == Action::volume_down ? -10 : 10),0,100);
        cue("ses_buton_3"); break;
    case Action::brightness_down: case Action::brightness_up:
        settings_.brightness = std::clamp(settings_.brightness + (action == Action::brightness_down ? -5 : 5),-50,50);
        cue("ses_buton_3"); break;
    case Action::local: case Action::no_ghost: {
        const int selected = action == Action::local ? 1 : 0;
        if (settings_.ghost != selected) cue("ses_buton_4");
        settings_.ghost = selected;
        break;
    }
    case Action::start: screen_ = origin; transition(Screen::loading, UiCommand::play); break;
    case Action::quit: screen_ = origin; transition(Screen::hidden, UiCommand::quit); break;
    case Action::resume: cue("buton_1_wav"); screen_ = Screen::hidden; return UiCommand::resume;
    case Action::restart: case Action::menu:
        cue("buton_1_wav"); confirmation_return_ = origin;
        screen_ = action == Action::restart ? Screen::confirm_restart : Screen::confirm_menu;
        focus_ = hover_ = -1; break;
    case Action::no:
        cue("buton_1_wav"); confirmation_return_ = return_screen; screen_ = return_screen;
        focus_ = hover_ = -1; break;
    case Action::yes:
        cue("buton_1_wav");
        mouse_mode_write_ = v(0); // Both result and smart-menu YES callbacks, before their fade.
        screen_ = origin; confirmation_return_ = return_screen;
        command_ = origin == Screen::confirm_restart ? UiCommand::restart : UiCommand::menu;
        transition_ = Transition::command_fade; fade_ = v(0);
        fade_progress_ = v(0);
        focus_ = hover_ = -1;
        advance(current_ticks_,seconds_); // First wait displays alpha0 but advances the local counter.
        break;
    case Action::graphics:
        cue("buton_1_wav");
        graphics_open_request_ = true;
        break;
    }
    return UiCommand::none;
}

int MenuPanel::observe_pointer(const MenuInput& input) noexcept {
    int pointer_transition = 0;
    if (input.pointer_enabled) {
        if (!previous_ui_down_ && input.pointer_pressed) pointer_transition = 1;
        else if (previous_ui_down_ && !input.pointer_down) pointer_transition = 2;
        previous_ui_down_ = input.pointer_down; // Native history is saved before callbacks and gates.
    }
    return pointer_transition;
}

game::UiCommand MenuPanel::input(const MenuInput& input) {
    using game::UiCommand;
    const int pointer_transition = observe_pointer(input); // Save global held history before every gate/callback.
    pointer_cursor_x_ = physics::property(input.x); pointer_cursor_y_ = physics::property(input.y);
    initialize_source_panels();
    const int graphics_hit = hit_graphics(input); // Prior rendered native extension layout, just like preflight.
    hover_ = -1;
    if (ready() && screen_ != Screen::help) {
        build_buttons(screen_,canvas_x(354));
        if (input.tab && button_count_) {
            focus_ = input.shift ? (focus_ <= 0 ? static_cast<int>(button_count_)-1 : focus_-1)
                : (focus_+1) % static_cast<int>(button_count_);
            cue(menu_visible() ? "ses_buton_1" : "buton_0_wav");
        }
    }
    if (input.pointer_enabled) {
        previous_panel_ = current_panel_;
        current_panel_ = select_source_panel(input); // Independent of MOUSE_VALID, before dispatch.
        PointerDispatch result;
        if (current_panel_ >= 0)
            result = dispatch_source_panel(static_cast<std::size_t>(current_panel_),input,pointer_transition);
        if (!result.consumed && previous_panel_ >= 0 && previous_panel_ != current_panel_)
            result = dispatch_source_panel(static_cast<std::size_t>(previous_panel_),input,pointer_transition);
        // The previous helper's consume does not retain it as the current candidate.
        if (result.first_callback) return result.command; // Pointer FIRST takes precedence over Enter.
        if (graphics_hit >= 0 && ready()) {
            hover_ = graphics_hit;
            if (pointer_transition == 1) {
                focus_ = graphics_hit;
                const auto command = activate(Action::graphics,screen_,confirmation_return_);
                graphics_transient_ = 2;
                return command;
            }
            if (pointer_transition == 2) graphics_transient_ = -2;
            else if (input.pointer_down) graphics_transient_ = 2;
            else {
                if (graphics_transient_ == 0) cue(menu_visible() ? "ses_buton_1" : "buton_0_wav");
                graphics_transient_ = -3;
            }
        } else graphics_transient_ = 0;
    }
    // Source callbacks above are governed by PANEL visibility/previous candidate, not a global ready gate.
    // Accessibility remains current-ready only, including pointer-disabled passes.
    if (!ready() || screen_ == Screen::help) return UiCommand::none;
    if (input.enter && focus_ >= 0 && static_cast<std::size_t>(focus_) < button_count_)
        return activate(buttons_[focus_].action,screen_,confirmation_return_);
    if (screen_ == Screen::color && (input.left || input.right))
        return activate(input.left ? Action::color_left : Action::color_right,screen_,confirmation_return_);
    if (screen_ == Screen::settings && focus_ >= 0 && focus_ < 4 && (input.left || input.right)) {
        const bool volume = focus_ < 2;
        return activate(volume ? (input.left ? Action::volume_down : Action::volume_up)
            : (input.left ? Action::brightness_down : Action::brightness_up),screen_,confirmation_return_);
    }
    // ESC is source-owned smart_menu_start: it cannot resume pause or dismiss results/confirmation.
    return UiCommand::none;
}

void MenuPanel::persist_settings() {
    const auto path = settings_path();
    if (path.empty()) { persistence_error_ = "Ayarlar kaydedilemedi: kullanıcı dizini yok."; return; }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(),error);
    if (error) { persistence_error_ = "Ayarlar kaydedilemedi: " + error.message(); return; }
    auto temp = path; temp += ".tmp";
    std::ofstream out(temp, std::ios::trunc);
    out.imbue(std::locale::classic());
    out << settings_.volume << ' ' << settings_.brightness << ' ' << settings_.music << ' '
        << settings_.color << ' ' << settings_.ghost << " 666 ";
    out.close();
    if (!out) { persistence_error_ = "Ayarlar kaydedilemedi."; std::filesystem::remove(temp,error); return; }
    std::filesystem::rename(temp,path,error);
    if (error) { persistence_error_ = "Ayarlar kaydedilemedi: " + error.message(); std::filesystem::remove(temp,error); }
}

void MenuPanel::text(Painter& painter, std::string_view value, float x, float y, bool centered) const {
    painter.bitmap_text(time_font_,font_width[resources_.tier()],font_height[resources_.tier()],value,x,y,centered);
}

void MenuPanel::draw_panel_image(Painter& painter, PanelId id, std::uint32_t image, float alpha, bool filtered) const {
    const auto geometry = source_panel(panel_index(id));
    painter.set_layer(geometry.layer);
    painter.set_source_order(geometry.source_order);
    painter.image(image,pixel(geometry.x),pixel(geometry.y),pixel(geometry.sx),pixel(geometry.sy),alpha,filtered);
}

void MenuPanel::draw_screen(Painter& painter, Screen screen, Var x, bool active) {
    const auto index = screen <= Screen::exit
        ? panel_index(PanelId::main_inert)+static_cast<std::size_t>(screen)*2+(active ? 1 : 0)
        : active_panel(screen);
    const auto& panel = native_panels_[index];
    const auto geometry = source_panel(index);
    painter.set_layer(panel.layer); painter.set_source_order(panel.source_order);
    build_buttons(screen,x); // Keyboard layout is separate from full source child declaration order.
    for (std::size_t i = 0; i < panel.button_count; ++i) {
        const auto& button = panel.buttons[i];
        const int transient = rendered_button_transients_[index][i];
        const bool focused = active && button.keyboard_index >= 0 && focus_ == button.keyboard_index;
        const std::size_t slot = transient > 0 ? 2 : transient < 0 || focused ? 1 : 0;
        painter.image(source_button_image(index,i,slot),
                      pixel(geometry.x+v(button.x)*geometry.sx),pixel(geometry.y+v(button.y)*geometry.sy),
                      pixel(geometry.sx),pixel(geometry.sy));
    }
    for (std::size_t i = 0; i < button_count_; ++i) {
        const auto& button = buttons_[i];
        if (button.action != Action::graphics) continue;
        painter.set_source_order(0); // Native Graphics is not a fabricated authored PANEL/button.
        const bool highlighted = active && (rendered_graphics_transient_ < 0 || focus_ == static_cast<int>(i));
        const float border = highlighted ? 2.f : 1.f;
        painter.solid(white_,button.x,button.y,button.native_width,button.native_height,{0,0,0,1});
        painter.solid(white_,button.x+border,button.y+border,button.native_width-2*border,
                      button.native_height-2*border,{1,1,1,1});
        const auto begin = painter.quads.size();
        text(painter,"GRAFİK / Graphics",button.x+button.native_width*.5f,
             button.y+(button.native_height-font_height[resources_.tier()])*.5f,true);
        for (std::size_t quad = begin; quad < painter.quads.size(); ++quad)
            painter.quads[quad].color = {0,0,0,1};
        painter.set_source_order(panel.source_order);
    }
    if (active && screen == Screen::credits) {
        painter.set_layer(18);
        painter.set_source_order(source_ui_order(2,183));
        const auto start = painter.quads.size();
        text(painter,credits,pixel(canvas_x(498)),pixel(canvas_y(210)-credits_offset_),true);
        clip_text(painter,start,pixel(canvas_y(210)),pixel(canvas_y(210+386)));
    }
}

void MenuPanel::draw(Painter& painter) {
    if (transition_ == Transition::menu_return_wait) {
        // Source cleanup hides all result/pause panes before its final black-only yield.
        painter.set_layer(99); painter.set_source_order(source_ui_order(0,201));
        draw_panel_image(painter,PanelId::curtain,fade_image_,opacity(fade_),false);
        return;
    }
    if (screen_ == Screen::hidden) {
        if (result_backdrop_visible_) {
            painter.set_layer(2); painter.set_source_order(source_ui_order(4,941));
            draw_panel_image(painter,PanelId::result_bg,
                             resources_.find(bloom_ ? "GO_sonuc_fon1.tga" : "GO_sonuc_fon0.tga"),opacity(result_backdrop_alpha_));
        }
        return;
    }
    if (screen_ == Screen::loading) {
        if (menu_art_visible_) draw_menu_art(painter);
        painter.set_layer(99); painter.set_source_order(source_ui_order(0,201));
        draw_panel_image(painter,PanelId::curtain,fade_image_,opacity(fade_),false);
        draw_panel_image(painter,PanelId::loading_bg,resources_.find("loading_ekran1.tga"));
        draw_panel_image(painter,PanelId::smoke1,resources_.find("load_duman_0.tga"));
        draw_panel_image(painter,PanelId::smoke2,resources_.find("load_duman_1.tga"));
        if (!persistence_error_.empty()) {
            painter.set_layer(201);
            painter.set_source_order(0);
            text(painter,persistence_error_,pixel(canvas_x(512)),pixel(canvas_y(720)),true);
        }
        if (loading_bar_visible_) {
            painter.set_layer(999);
            painter.set_source_order(source_ui_order(2,2903));
            painter.solid(white_,0,pixel(canvas_y(734)),pixel(loading_width_),pixel(canvas_y(10)),{1,128.f/255,0,1});
        }
        return;
    }
    if (screen_ == Screen::help) {
        painter.set_layer(13); painter.set_source_order(source_ui_order(4,1864));
        draw_panel_image(painter,PanelId::help,resources_.find("ingame_help.tga")); return;
    }
    if (!menu_visible()) {
        const bool result = screen_ == Screen::results ||
            ((screen_ == Screen::confirm_restart || screen_ == Screen::confirm_menu) && confirmation_return_ == Screen::results);
        painter.set_layer(result ? 2 : 12);
        painter.set_source_order(source_ui_order(4,result ? 941 : 1616));
        if (!result || result_backdrop_visible_)
            draw_panel_image(painter,result ? PanelId::result_bg : PanelId::pause_bg,
                             resources_.find(bloom_ ? "GO_sonuc_fon1.tga" : "GO_sonuc_fon0.tga"),
                             result ? opacity(result_backdrop_alpha_) : 1);
        if (screen_ == Screen::results && !result_buttons_) return;
        painter.set_layer(result ? 3 : 13);
        painter.set_source_order(source_ui_order(4,result ? 948 : 1624));
        draw_panel_image(painter,result ? PanelId::result_base : PanelId::pause_base,
                         resources_.find(result ? "GO_sonuc_taban.tga" : "SM_taban.tga"));
        painter.set_layer(result ? 4 : 14);
        if (screen_ == Screen::results) {
            painter.set_source_order(source_ui_order(4,1030));
            text(painter,result_text_,pixel(canvas_x(280)),pixel(canvas_y(356)));
        }
        if ((screen_ == Screen::results || screen_ == Screen::pause) && !record_error_.empty()) {
            painter.set_source_order(0);
            const float error_y = screen_ == Screen::pause ? pixel(canvas_y(312))
                : pixel(canvas_y(356))+font_height[resources_.tier()]+2;
            text(painter,"NATIVE KAYIT HATASI:",pixel(canvas_x(280)),error_y);
            text(painter,record_error_,pixel(canvas_x(280)),error_y+font_height[resources_.tier()]+2);
        }
        if (screen_ == Screen::confirm_restart || screen_ == Screen::confirm_menu) {
            painter.set_source_order(source_ui_order(4,result ? 1047 : 1660));
            text(painter,screen_ == Screen::confirm_restart ? restart_question : menu_question,
                 pixel(canvas_x(question_x[resources_.tier()])),pixel(canvas_y(356)),true);
        }
        if (transition_ == Transition::idle) draw_screen(painter,screen_,canvas_x(354),ready());
        if (transition_ == Transition::command_fade || transition_ == Transition::command_wait
            || transition_ == Transition::result_restart_wait) {
            painter.set_layer(99);
            painter.set_source_order(source_ui_order(0,201));
            draw_panel_image(painter,PanelId::curtain,fade_image_,opacity(fade_),false);
        }
        return;
    }
    draw_menu_art(painter);
    if (fade_ > v(0)) {
        painter.set_layer(99); painter.set_source_order(source_ui_order(0,201));
        draw_panel_image(painter,PanelId::curtain,fade_image_,opacity(fade_),false);
    }
}

void MenuPanel::draw_menu_art(Painter& painter) {
    draw_panel_image(painter,PanelId::menu_black,fade_image_,1,false);
    draw_panel_image(painter,PanelId::menu_bottom,resources_.find("menu_tabanalt.tga"));
    draw_panel_image(painter,PanelId::tile1,resources_.find("menu_arka_tile1.tga"));
    draw_panel_image(painter,PanelId::tile2,resources_.find("menu_arka_tile2.tga"));
    if (screen_ <= Screen::exit && !ready() && !incoming_visible_ && transition_ != Transition::gap
        && transition_ != Transition::quit_wait && transition_ != Transition::quit_black_wait)
        draw_screen(painter,screen_,inert_x_[static_cast<std::size_t>(screen_)],false);
    if (incoming_visible_)
        draw_screen(painter,target_,inert_x_[static_cast<std::size_t>(target_)],false);
    draw_panel_image(painter,PanelId::menu_right,resources_.find("menu_tabansag.tga"));
    draw_panel_image(painter,PanelId::shutter,resources_.find("menu_kepeng.tga"));
    for (const auto id : {PanelId::arrow1a,PanelId::arrow1b,PanelId::arrow2a,PanelId::arrow2b})
        draw_panel_image(painter,id,resources_.find("menu_c4_oklar.tga"),.05f);
    draw_panel_image(painter,PanelId::tv,resources_.find("menu_tvekran.tga"));
    draw_panel_image(painter,PanelId::top,resources_.find("menu_ust_cita.tga"));
    draw_panel_image(painter,PanelId::video,resources_.find("menu_videoekran.tga"));
    draw_panel_image(painter,PanelId::front,resources_.find("menu_on_panel.tga"));
    draw_panel_image(painter,PanelId::car,cars_[settings_.color]);
    if (ready()) draw_screen(painter,screen_,canvas_x(354),true);
    draw_panel_image(painter,PanelId::laser,resources_.find("menu_arac_LAZER.tga"),opacity(laser_alpha_));
    draw_panel_image(painter,PanelId::robot1,robots1_[settings_.color]);
    draw_panel_image(painter,PanelId::robot2,robots2_[settings_.color]);
    if (ready() && !record_error_.empty()) {
        painter.set_layer(20);
        painter.set_source_order(0);
        text(painter,"NATIVE KAYIT HATASI:",pixel(canvas_x(512)),pixel(canvas_y(688)),true);
        text(painter,record_error_,pixel(canvas_x(512)),pixel(canvas_y(688))+font_height[resources_.tier()]+2,true);
    }
    if (ready() && screen_ == Screen::scores) {
        painter.set_layer(22);
        painter.set_source_order(0);
        text(painter,"ÇEVRİMDIŞI MOD\nOrijinal HTTP bağımlılığı kullanılamıyor.\nPOST http://77.92.136.178/\noyun_sunucu/c4_sfx_high.php",
             pixel(canvas_x(498)),pixel(canvas_y(230)),true);
    }
}
} // namespace c4::ui
