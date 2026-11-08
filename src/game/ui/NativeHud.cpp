#include "NativeHud.hpp"

#include <charconv>
#include <string_view>

namespace c4::ui {
using physics::v;
using physics::number;
using physics::var_abs;
using physics::var_integer;
using physics::var_min;
using physics::var_max;
using physics::var_modulo;
using physics::var_sign;
using physics::var_sin;
namespace {
constexpr std::array<std::string_view,4> digital_fonts{
    "font_digital8_0.bmp", "font_digital8_1.bmp", "font_digital8_2.bmp", "font_digital8_3.bmp"};
constexpr std::array<std::array<int,2>,4> digital_cells{{{8,11}, {10,13}, {13,17}, {16,21}}};
constexpr std::array<std::string_view,4> time_fonts{
    "time_beyazfont_0.tga", "time_beyazfont_1.tga", "time_beyazfont_2.tga", "time_beyazfont_3.tga"};
constexpr std::array<std::array<int,2>,4> time_cells{{{5,10}, {7,14}, {9,19}, {11,23}}};
constexpr std::array<int,4> speed_x_offsets{0,2,1,0};
constexpr std::array<int,4> speed_y_offsets{0,2,0,0};
constexpr std::array<int,4> gear_x_offsets{1,2,1,1};
constexpr std::array<int,4> gear_y_offsets{0,1,0,0};
constexpr std::array<int,4> name_y_offsets{1,1,0,1};
constexpr std::array<int,4> time_y_offsets{1,0,0,0};
constexpr std::array<int,4> lap_y_offsets{1,1,0,0};
constexpr char source_gear(int number, char previous) noexcept {
    if (number == 0) return 'N';
    if (number >= 1 && number <= 5) return static_cast<char>('0' + number);
    if (number == 7) return 'R';
    return previous; // The original has no mapping for gear 6.
}
static_assert(source_gear(0, 'R') == 'N' && source_gear(5, 'N') == '5');
static_assert(source_gear(6, '5') == '5' && source_gear(7, '5') == 'R');
} // namespace

NativeHud::NativeHud(Resources& resources)
    : resources_(resources),
      base_(resources.add("hizpanel_taban.tga")),
      bakla_(resources.add("hizpanel_bakla.tga")),
      turbo_(resources.add("hizpanel_turbo.tga")),
      time_right_(resources.add("time_panel_sag.tga")),
      time_left_(resources.add("time_panel_sol.tga")),
      checkpoint_red_(resources.add("checkpoint_red.tga")),
      checkpoint_green_(resources.add("checkpoint_green.tga")),
      digital_font_(resources.add(digital_fonts[resources.tier()], true, true)),
      time_font_(resources.add(time_fonts[resources.tier()], false, true)) {
    const auto sx = v(resources.scale_x()), sy = v(resources.scale_y());
    const auto right = v(resources.canvas_width() - 1), bottom = v(resources.canvas_height() - 1);
    source_flags_[12] = source_flags_[13] = true;
    panel_image_ids_ = {base_, bakla_, turbo_, time_right_, time_left_, checkpoint_red_};
    source_panels_[0] = {right, bottom, -sx, -sy, 0, 0, 3, source_ui_order(1, 37), false};
    source_panels_[1] = {right, bottom, -sx, -sy, 0, 0, 2, source_ui_order(1, 44), false};
    source_panels_[2] = {v(820) * sx, v(734) * sy, v(1), sy, 0, 0, 2, source_ui_order(1, 58), false};
    source_panels_[3] = {right, v(0), -sx, sy, 0, 0, 2, source_ui_order(1, 127), false};
    source_panels_[4] = {v(0), v(0), sx, sy, 0, 0, 2, source_ui_order(1, 134), false};
    source_panels_[5] = {v(325) * sx, v(0), sx, sy, 0, 0, 2, source_ui_order(1, 226), false};
}

const std::array<SourcePanelPickState,6>& NativeHud::source_panels() const {
    refresh_source_panels();
    return source_panels_;
}

void NativeHud::refresh_source_panels() const {
    constexpr std::array<std::size_t,6> visibility_slots{11, 10, 9, 5, 6, 15};
    const auto checkpoint_image = checkpoint_good_ ? checkpoint_green_ : checkpoint_red_;
    if (panel_image_ids_[5] != checkpoint_image) {
        panel_image_ids_[5] = checkpoint_image;
        source_panels_[5].width = source_panels_[5].height = 0;
    }
    for (std::size_t panel = 0; panel < source_panels_.size(); ++panel) {
        auto& picked = source_panels_[panel];
        if (picked.width == 0 && panel_image_ids_[panel] < resources_.images.size()) {
            const auto& image = resources_.image(panel_image_ids_[panel]);
            picked.width = image.width;
            picked.height = image.height;
        }
        if (picked.visible != source_flags_[visibility_slots[panel]])
            picked.visible = source_flags_[visibility_slots[panel]];
    }
    // Original picking ignores alpha/transparent state: cockpit turbo retains
    // its PANEL's VISIBLE flag even while hiz_panel_kapat makes its alpha zero.
    if (source_panels_[2].sx != turbo_scale_) source_panels_[2].sx = turbo_scale_;
    if (source_panels_[5].y != checkpoint_y_) source_panels_[5].y = checkpoint_y_;
}

void NativeHud::sync_source_visibility(const game::SharedState& state) {
    state_ = &state;
    const bool source_loop_open = state.oyunici_panel_acik == v(1);
    if (source_loop_open && !visibility_loop_open_) {
        source_flags_.fill(true);
        source_flags_[9] = false;
        source_flags_[12] = source_flags_[13] = false;
        source_flags_[14] = source_flags_[15] = false;
        old_speed_visibility_ = true;
        turbo_opaque_ = true;
    }
    visibility_loop_open_ = source_loop_open;
    if (state.hud_off_event_counter != hud_off_event_) {
        hud_off_event_ = state.hud_off_event_counter;
        saved_flags_ = source_flags_;
        source_flags_.fill(false);
        source_flags_[12] = source_flags_[13] = true;
        // The OFF producer also clears legacy aggregates after its source
        // snapshot. Consume that proxy edge without calling hiz_panel_kapat.
        old_speed_visibility_ = state.hud_speed_visible;
        refresh_source_panels();
        return;
    }
    if (state.hud_speed_visible != old_speed_visibility_) {
        old_speed_visibility_ = state.hud_speed_visible;
        source_flags_[7] = source_flags_[8] = state.hud_speed_visible;
        source_flags_[10] = source_flags_[11] = state.hud_speed_visible;
        source_flags_[12] = source_flags_[13] = !state.hud_speed_visible;
        // hiz_panel_kapat changes turbo alpha, NOT its saved VISIBLE bit.
        turbo_opaque_ = state.hud_speed_visible;
    }
    refresh_source_panels();
}

void NativeHud::restore_source_panels(game::SharedState& state) {
    sync_source_visibility(state);
    source_flags_ = saved_flags_;
    state.hud_speed_visible = source_flags_[8];
    old_speed_visibility_ = state.hud_speed_visible;
    state.hud_visible = !source_flags_[12] || !source_flags_[13];
    for (std::size_t slot = 0; slot < source_flags_.size(); ++slot)
        if (slot != 12 && slot != 13) state.hud_visible = state.hud_visible || source_flags_[slot];
    refresh_source_panels();
}

void NativeHud::update(const game::SharedState& state, game::FrameTiming timing) {
    sync_source_visibility(state);
    if (state.oyunici_panel_acik != v(1)) {
        loop_open_ = false;
        loop_waiting_ = false;
        if (checkpoint_stage_ != CheckpointStage::hidden)
            source_flags_[14] = source_flags_[15] = false;
        checkpoint_stage_ = CheckpointStage::hidden;
        checkpoint_event_ = state.checkpoint_event_counter;
        refresh_source_panels();
        return;
    }
    if (!loop_open_) {
        loop_open_ = true;
        loop_waiting_ = false;
        needle_roll_ = v(50);
        needle_lower_limit_ = v(-230);
        needle_phase_ = v(0);
        speed_[0] = '0';
        speed_length_ = 1;
        gear_ = 'N';
        old_gear_ = v(0);
        turbo_scale_ = v(1);
    }

    const game::Var time = timing.originaltime_step_ticks;
    if (state.ghost_reset_ok == v(1)) {
        if (checkpoint_stage_ != CheckpointStage::hidden)
            source_flags_[14] = source_flags_[15] = false;
        checkpoint_stage_ = CheckpointStage::hidden;
        checkpoint_event_ = state.checkpoint_event_counter;
    } else if (checkpoint_event_ != state.checkpoint_event_counter) {
        checkpoint_event_ = state.checkpoint_event_counter;
        if (state.active_ghost && state.checkpoint_event_no >= 1 && state.checkpoint_event_no <= 9) {
            checkpoint_stage_ = CheckpointStage::entering;
            source_flags_[15] = true;
            checkpoint_progress_ = v(0);
            checkpoint_good_ = state.checkpoint_delta_ticks <= v(0);
        }
    }
    // check_point_goster is a separate source process: cockpit and motor waits
    // do not stop it, and its text appears only during the three-second hold.
    if (checkpoint_stage_ == CheckpointStage::entering && checkpoint_progress_ > v(100)) {
        checkpoint_stage_ = CheckpointStage::holding;
        source_flags_[14] = true;
        checkpoint_progress_ = v(0);
    } else if (checkpoint_stage_ == CheckpointStage::holding && checkpoint_progress_ > v(100)) {
        checkpoint_stage_ = CheckpointStage::leaving;
        source_flags_[14] = false;
        checkpoint_progress_ = v(0);
    } else if (checkpoint_stage_ == CheckpointStage::leaving && checkpoint_progress_ > v(100)) {
        checkpoint_stage_ = CheckpointStage::hidden;
        source_flags_[15] = false;
    }
    switch (checkpoint_stage_) {
    case CheckpointStage::entering:
        checkpoint_y_ = (v(-39) + v(39) * checkpoint_progress_ / v(100)) * v(resources_.scale_y());
        checkpoint_progress_ += v(6.25) / v(.25) * time;
        break;
    case CheckpointStage::holding:
        checkpoint_y_ = v(0);
        checkpoint_progress_ += v(6.25) / v(3) * time;
        break;
    case CheckpointStage::leaving:
        checkpoint_y_ = (v(0) - v(39) * checkpoint_progress_ / v(100)) * v(resources_.scale_y());
        checkpoint_progress_ += v(6.25) / v(.25) * time;
        break;
    case CheckpointStage::hidden:
        break;
    }

    const bool blocked = state.ghost_reset_ok == v(1) || state.freeze_mode > v(0) || state.gameover_panel_acik == v(1);
    // Source updates once before entering its wait; the release iteration
    // finishes that wait and reaches wait(1), without another gauge update.
    if (loop_waiting_) {
        if (!blocked) loop_waiting_ = false;
        refresh_source_panels();
        return;
    }
    const game::Var speed = var_abs(state.arac_ivme) * v(.8);
    const game::Var target = v(50) - speed;
    const game::Var old_roll = needle_roll_;
    needle_roll_ += v(.5) * time * (target - needle_roll_);
    needle_roll_ = var_min(var_max(needle_roll_, needle_lower_limit_), v(50));
    if (var_sign(target - old_roll) != var_sign(target - needle_roll_)) needle_roll_ = target;
    needle_lower_limit_ = v(-230) - var_abs(v(3) * var_sin(needle_phase_));
    needle_phase_ += v(6.25) / v(.06) * time;
    needle_phase_ = var_modulo(needle_phase_, v(360));
    // The full turbo bitmap is stretched; 186 is the original visibility test.
    const game::Var turbo_fraction = v(1) * (state.turbo_ekle_miktar / v(300)) * v(resources_.scale_x());
    source_flags_[9] = turbo_fraction * v(186) >= v(1);
    if (source_flags_[9]) turbo_scale_ = turbo_fraction;
    const auto digits = std::to_chars(speed_.data(), speed_.data() + speed_.size(),
                                     static_cast<int>(number(var_integer(speed))));
    speed_length_ = static_cast<std::size_t>(digits.ptr - speed_.data());
    if (old_gear_ != state.arac_vites_no) {
        const game::Var integral_gear = var_integer(state.arac_vites_no);
        if (state.arac_vites_no == integral_gear)
            gear_ = source_gear(static_cast<int>(number(integral_gear)), gear_);
        old_gear_ = state.arac_vites_no;
    }
    loop_waiting_ = blocked;
    refresh_source_panels();
}

void NativeHud::draw(Painter& painter) const {
    if (!state_) return;
    refresh_source_panels();
    const auto tier = resources_.tier();
    const auto sx = v(resources_.scale_x()), sy = v(resources_.scale_y());
    const auto x = [sx](int value) { return static_cast<float>(number(v(value) * sx)); };
    const auto y = [sy](int value, int offset = 0) {
        return static_cast<float>(number(v(value) * sy + v(offset)));
    };
    const auto panel = [&](std::size_t index) {
        const auto& source = source_panels_[index];
        painter.set_layer(source.layer);
        painter.set_source_order(source.source_order);
        if (source.visible)
            painter.image(panel_image_ids_[index], static_cast<float>(number(source.x)),
                          static_cast<float>(number(source.y)), static_cast<float>(number(source.sx)),
                          static_cast<float>(number(source.sy)));
    };
    // Draw and picking share the original Q10 PANEL coordinates, including mirrors.
    panel(1);
    if (turbo_opaque_) panel(2);
    panel(3);
    panel(4);
    panel(5);
    panel(0);
    const auto& time_cell = time_cells[tier];
    painter.set_layer(3);
    painter.set_source_order(source_ui_order(1, 195));
    if (source_flags_[4]) painter.bitmap_text(time_font_, time_cell[0], time_cell[1], state_->rakip_ismi,
                                             x(18), y(5, name_y_offsets[tier]));
    painter.set_source_order(source_ui_order(1, 211));
    if (source_flags_[3]) painter.bitmap_text(time_font_, time_cell[0], time_cell[1], state_->timepanel_rekor_str,
                                             x(18), y(31, time_y_offsets[tier]));
    painter.set_source_order(source_ui_order(1, 145));
    if (source_flags_[2]) painter.bitmap_text(time_font_, time_cell[0], time_cell[1], state_->oyuncu_ismi,
                                             x(880), y(5, name_y_offsets[tier]));
    painter.set_source_order(source_ui_order(1, 162));
    if (source_flags_[1]) painter.bitmap_text(time_font_, time_cell[0], time_cell[1], state_->timepanel_time_str,
                                             x(880), y(31, time_y_offsets[tier]));
    painter.set_source_order(source_ui_order(1, 178));
    if (source_flags_[0]) painter.bitmap_text(time_font_, time_cell[0], time_cell[1], state_->timepanel_lap_str,
                                             x(880), y(56, lap_y_offsets[tier]));
    painter.set_source_order(source_ui_order(1, 236));
    if (source_flags_[14])
        painter.bitmap_text(time_font_, time_cell[0], time_cell[1], state_->timepanel_check_str, x(377), y(5));
    const auto& digital_cell = digital_cells[tier];
    painter.set_layer(4);
    painter.set_source_order(source_ui_order(1, 94));
    if (source_flags_[8])
        painter.bitmap_text(digital_font_, digital_cell[0], digital_cell[1], {speed_.data(), speed_length_},
                            x(915 + speed_x_offsets[tier]), y(697 + speed_y_offsets[tier]), true, true);
    painter.set_source_order(source_ui_order(1, 112));
    if (source_flags_[7])
        painter.bitmap_text(digital_font_, digital_cell[0], digital_cell[1], {&gear_, 1},
                            x(915 + gear_x_offsets[tier]), y(603 + gear_y_offsets[tier]), true, true);
}
} // namespace c4::ui
