#pragma once

#include "physics/FixedPoint.hpp"

#include <memory>

namespace c4::game { struct GameContext; }

namespace c4::rendering {
struct RenderScene;
struct NativeGameVisualState;
struct SourceSpriteView;

// Construct on the final world after source bootstrap/runtime initialization and
// before either backend uploads meshes or textures. The scene must outlive this
// bridge and must not be moved or structurally modified afterward.
class GameRenderBridge {
public:
    GameRenderBridge(RenderScene& scene, game::GameContext& context);
    ~GameRenderBridge();
    GameRenderBridge(const GameRenderBridge&) = delete;
    GameRenderBridge& operator=(const GameRenderBridge&) = delete;

    // Ordinary source process boundary: commit writes before live wait1 predicates.
    void advance_material_processes(physics::Var time_step_ticks, physics::Var time_frame_ticks);
    // Source predraw before UI1: reconcile cached Euler and changed MATERIAL identity.
    void reconcile_material_events(physics::Var time_step_ticks);
    // Render synchronization publishes poses/bindings without running processes.
    void synchronize();
    // Actual source enable_view callback; never advances scheduler/material time.
    void synchronize_view(const NativeGameVisualState& visuals, const SourceSpriteView& sprite_view);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace c4::rendering
