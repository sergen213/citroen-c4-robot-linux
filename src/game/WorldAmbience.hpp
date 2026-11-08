#pragma once

#include "GameState.hpp"

namespace c4::game {
// new_color.wdl84-128, retaining script-Q10 operation order.
Var source_sound_pan(ScriptVec source, ScriptVec camera, Var camera_pan);
Var source_sound_volume(ScriptVec source, ScriptVec camera, Var target_z, Var maximum_distance);
// c4_effects.wdl793-800: only the eligible helicopter action tunes this HUD-owned voice.
void tune_world_helicopter(GameContext& context, EntityId body);

class WorldAmbience {
public:
    explicit WorldAmbience(GameContext& context);
    void initialize(GameContext& context, FrameTiming timing);
    void teardown(GameContext& context);
    void update(GameContext& context, FrameTiming timing);
private:
    enum class Stage { awaiting_hud, running, suppressed };
    std::array<EntityId,7> points_{};
    std::array<audio::Handle,7> spatial_handles_{};
    Stage stage_{Stage::awaiting_hud};
    bool initialized_{}, processed_{};
    std::uint64_t last_frame_{};
    void stop(GameContext& context);
};
} // namespace c4::game
