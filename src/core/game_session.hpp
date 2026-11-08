#pragma once

#include "assets/GpuImage.hpp"
#include "game/GameState.hpp"
#include "game/ui/NativeUi.hpp"
#include "render_scene.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace c4 {

// Owns the world and every borrower; the final render scene never moves.
class GameSession {
public:
    GameSession(const Settings& settings, const assets::GpuImageRuntimeContext& file_context);
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    rendering::RenderScene& scene();
    ui::NativeUi& ui();
    void set_viewport(int pixel_width, int pixel_height, bool fullscreen);
    void update(game::InputSnapshot input, const ui::MenuInput& menu, double seconds_deadlines);
    rendering::FrameSnapshot frame(int pixel_width, int pixel_height) const;
    bool finished() const;
    std::uint64_t source_frames() const;
    bool input_scenario_finished() const;
    std::string diagnostics() const;
    audio::Audio& audio();
    bool cursor_visible() const;
    physics::Var source_mouse_mode() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace c4
