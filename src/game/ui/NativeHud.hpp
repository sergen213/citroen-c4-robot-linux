#pragma once

#include "UiResources.hpp"
#include "game/GameState.hpp"

#include <array>
#include <cstdint>
#include <cstddef>

namespace c4::ui {
class NativeHud {
public:
    explicit NativeHud(Resources& resources);
    void update(const game::SharedState& state, game::FrameTiming timing);
    void sync_source_visibility(const game::SharedState& state);
    void restore_source_panels(game::SharedState& state);
    // Source slots 12/13 are INVISIBLE; the other fourteen are VISIBLE.
    const std::array<bool,16>& source_visibility() const noexcept { return source_flags_; }
    const std::array<SourcePanelPickState,6>& source_panels() const;
    void draw(Painter& painter) const;
    double needle_roll() const noexcept { return physics::number(needle_roll_); }
    double gauge_frame() const noexcept { return state_ ? physics::number(state_->hud_vites_frame) : 0; }
    game::Var source_needle_roll() const noexcept { return needle_roll_; }

private:
    enum class CheckpointStage { hidden, entering, holding, leaving };
    void refresh_source_panels() const;
    const game::SharedState* state_{};
    Resources& resources_;
    std::uint32_t base_{}, bakla_{}, turbo_{}, time_right_{}, time_left_{};
    std::uint32_t checkpoint_red_{}, checkpoint_green_{}, digital_font_{}, time_font_{};
    std::array<char, 32> speed_{'0'};
    std::size_t speed_length_{1};
    char gear_{'N'};
    game::Var old_gear_{};
    bool loop_open_{}, loop_waiting_{}, visibility_loop_open_{}, old_speed_visibility_{true}, turbo_opaque_{true};
    game::Var turbo_scale_{game::Var::literal(1)};
    game::Var needle_roll_{game::Var::literal(50)}, needle_lower_limit_{game::Var::literal(-230)}, needle_phase_{};
    std::uint64_t checkpoint_event_{};
    CheckpointStage checkpoint_stage_{CheckpointStage::hidden};
    game::Var checkpoint_progress_{}, checkpoint_y_{};
    bool checkpoint_good_{};
    std::array<bool,16> source_flags_{}, saved_flags_{};
    std::uint64_t hud_off_event_{};
    mutable std::array<SourcePanelPickState,6> source_panels_{};
    mutable std::array<std::uint32_t,6> panel_image_ids_{};
};
} // namespace c4::ui
