#pragma once

#include "physics/NativeNumeric.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace c4::game {
struct GameContext;
struct FrameTiming;

// Prepared geometry identity is stable before PhysicsWorld borrows the scene.
// Script callbacks alone make these helpers live and consume source random.
struct SourceLeafStartup {
    std::uint32_t entity{}, parent{};
    physics::ScriptVec exact_spawn_position{};
    physics::Var height{}, scale{}, rotation_range{}, trunk_min_scale{};
};

class WorldLeaves {
public:
    struct Coverage {
        std::size_t trees{}, prepared{}, pending_trees{}, born{}, initialized{}, pending_callbacks{};
        bool queued{};
    };
    explicit WorldLeaves(GameContext& context);
    void initialize(GameContext& context, FrameTiming timing);
    void update(GameContext& context, FrameTiming timing);
    void teardown(GameContext& context);
    std::span<const std::uint32_t> bound_entities() const { return root_entities_; }
    std::span<const std::uint32_t> leaf_entities() const { return leaf_entities_; }
    std::span<const std::uint32_t> spawned_entities() const { return spawned_entities_; }
    bool dispatch_entity(GameContext& context, FrameTiming timing, std::uint32_t id);
    Coverage coverage() const;
private:
    struct Tree {
        std::uint32_t id{};
        std::size_t first_leaf{}, leaf_count{};
    };
    enum class TreeStage { entry, waiting, complete };
    struct TreeState {
        TreeStage stage{TreeStage::entry};
        std::uint64_t yielded_frame{};
        physics::Var sprite_scale{};
    };
    struct LeafState {
        bool born{}, initialized{};
        std::uint64_t born_frame{};
    };
    struct Process {
        std::size_t index{};
        bool leaf{};
    };
    std::span<const SourceLeafStartup> startup_;
    std::vector<Tree> trees_;
    std::vector<TreeState> tree_state_;
    std::vector<LeafState> leaf_state_;
    std::vector<std::uint32_t> root_entities_, leaf_entities_, spawned_entities_;
    std::vector<Process> entity_process_;
    std::vector<std::uint32_t> queue_, next_queue_;
    bool active_{};
    std::uint64_t last_frame_{};
    bool dispatch_tree(GameContext& context, FrameTiming timing, std::size_t index);
    bool dispatch_leaf(GameContext& context, FrameTiming timing, std::size_t index);
};
} // namespace c4::game
