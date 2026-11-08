#include "WorldLeaves.hpp"
#include "GameState.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace c4::game {
namespace {
using physics::v;
constexpr std::uint32_t invisible = 0x100u, passable = 0x200u;
constexpr std::uint32_t transparent = 0x400u, oriented = 0x1000u;
constexpr std::uint32_t facing = 0x2000u, decal = 0x400000u;
constexpr std::size_t unbound = std::numeric_limits<std::size_t>::max();
void flags(assets::WorldEntity& entity, std::uint32_t set, std::uint32_t clear = 0) {
    entity.source_flags_set = (entity.source_flags_set | set) & ~clear;
    entity.source_flags_clear = (entity.source_flags_clear | clear) & ~set;
}
void publish_position(assets::WorldEntity& entity, physics::Body& body, ScriptVec position) {
    body.position = physics::native(position);
    entity.source_position_words = std::array<std::int32_t, 3>{position.x.raw, position.y.raw, position.z.raw};
    entity.position = {position.x.to_float(), position.y.to_float(), position.z.to_float()};
}
} // namespace

WorldLeaves::WorldLeaves(GameContext& ctx): startup_(ctx.state.world_leaf_startup) {
    entity_process_.assign(ctx.world.entities.size(), Process{unbound, false});
    leaf_entities_.reserve(startup_.size());
    for (std::size_t index = 0; index < startup_.size(); ++index) {
        const auto& leaf = startup_[index];
        if (leaf.entity >= ctx.world.entities.size() || leaf.parent >= ctx.world.entities.size() ||
            leaf.entity == leaf.parent || ctx.world.entities[leaf.entity].action != "tl1_LeaveInit" ||
            entity_process_[leaf.entity].index != unbound)
            throw std::logic_error("Invalid prepared source leaf identity");
        if (trees_.empty() || trees_.back().id != leaf.parent) {
            if (!trees_.empty() && trees_.back().id >= leaf.parent)
                throw std::logic_error("Source leaves must retain parent/vertex creation order");
            trees_.push_back({leaf.parent, index, 0});
            root_entities_.push_back(leaf.parent);
        }
        ++trees_.back().leaf_count;
        leaf_entities_.push_back(leaf.entity);
        entity_process_[leaf.entity] = {index, true};
    }
    for (std::size_t index = 0; index < trees_.size(); ++index) {
        const auto id = trees_[index].id;
        if (entity_process_[id].index != unbound)
            throw std::logic_error("Prepared leaf identity aliases a source tree");
        entity_process_[id] = {index, false};
    }
    tree_state_.resize(trees_.size());
    leaf_state_.resize(startup_.size());
    const auto capacity = trees_.size() + startup_.size();
    queue_.reserve(capacity);
    next_queue_.reserve(capacity);
    std::size_t largest_tree = 0;
    for (const auto& tree : trees_) largest_tree = std::max(largest_tree, tree.leaf_count);
    spawned_entities_.reserve(largest_tree);
}

void WorldLeaves::initialize(GameContext&, FrameTiming timing) {
    if (active_) return;
    active_ = true;
    last_frame_ = timing.frame_index;
    queue_.assign(root_entities_.begin(), root_entities_.end());
    next_queue_.clear();
    spawned_entities_.clear();
    for (auto& tree : tree_state_) tree = {TreeStage::entry, timing.frame_index};
    for (auto& leaf : leaf_state_) leaf = {};
}

bool WorldLeaves::dispatch_tree(GameContext& ctx, FrameTiming timing, std::size_t index) {
    auto& state = tree_state_[index];
    if (state.stage == TreeStage::complete) return false;
    if (state.yielded_frame == timing.frame_index) return true;
    state.yielded_frame = timing.frame_index;
    if (state.stage == TreeStage::entry) {
        // canli_agaclar: each root's skin selection precedes its first wait1.
        // The selected model/skin is already cached; no RNG or leaf exists yet.
        state.stage = TreeStage::waiting;
        return true;
    }
    const auto& tree = trees_[index];
    const auto& first = startup_[tree.first_leaf];
    state.sprite_scale = first.scale;
    auto& parent = ctx.world.entities[tree.id];
    ctx.set_material(tree.id, "Mtl_agaclar");
    parent.ambient = -42;
    parent.source_ambient = -42;
    flags(parent, passable);
    ctx.physics.body(tree.id).flags.passable = true;
    parent.skills[9] = static_cast<float>(tree.leaf_count);
    parent.skills[10] = first.scale.to_float();
    parent.skills[11] = first.rotation_range.to_float();
    parent.skills[12] = first.height.to_float();
    for (std::size_t leaf_index = tree.first_leaf; leaf_index < tree.first_leaf + tree.leaf_count; ++leaf_index) {
        const auto& prepared = startup_[leaf_index];
        auto& entity = ctx.world.entities[prepared.entity];
        auto& body = ctx.physics.body(prepared.entity);
        auto& visual = ctx.visuals[prepared.entity];
        publish_position(entity, body, prepared.exact_spawn_position);
        entity.native_prepared_inactive = false;
        // ghidra/acknex.dll_36ae2132740b/decompiled/1003c2d0.c:12-23:
        // flags0, scales/frame1, alpha/albedo50, ambient0. Private +238
        // flags are NOT authored bits; NEXT_FRAME +3c remains zero.
        entity.flags = entity.source_flags_set = entity.source_flags_clear = 0;
        entity.angles = {};
        entity.scale = {1, 1, 1};
        entity.skills = {};
        entity.ambient = 0;
        entity.source_ambient = 0;
        entity.albedo = 50;
        entity.source_albedo = 50;
        entity.alpha_percent = 50;
        body.angles = {};
        body.scale = {1, 1, 1};
        body.flags.passable = false;
        // Resource attachment supplies the constructor's CURRENT baseline,
        // not a WDL assignment and not the creating trunk's MATERIAL.
        visual.material_identity = entity.source_initial_material_known
            ? entity.source_initial_material_identity : std::string_view{};
        visual.material_identity_known = entity.source_initial_material_known;
        visual.invisible = visual.transparent = false;
        visual.alpha = v(50);
        visual.frame = v(1);
        visual.next_frame = {};
        ++visual.revision;
        leaf_state_[leaf_index] = {true, false, timing.frame_index};
        spawned_entities_.push_back(prepared.entity);
    }
    state.stage = TreeStage::complete;
    return false;
}

bool WorldLeaves::dispatch_leaf(GameContext& ctx, FrameTiming timing, std::size_t index) {
    auto& state = leaf_state_[index];
    if (!state.born || state.initialized) return false;
    if (state.born_frame == timing.frame_index) return true;
    const auto& prepared = startup_[index];
    auto& entity = ctx.world.entities[prepared.entity];
    auto& body = ctx.physics.body(prepared.entity);
    auto& visual = ctx.visuals[prepared.entity];
    // canli_agaclar55-81: this callback is finite; its commented fade/sway
    // loop does not execute. This is its only random expression.
    entity.ambient = 50;
    entity.source_ambient = 50;
    flags(entity, passable | facing | decal, oriented | transparent);
    body.flags.passable = true;
    visual.transparent = false;
    visual.alpha = v(100);
    entity.alpha_percent = 100;
    auto angles = physics::body_source(body.angles);
    angles.z = ctx.random(prepared.rotation_range);
    body.angles = physics::native(angles);
    entity.angles[2] = angles.z.to_float();
    auto& scale = tree_state_[entity_process_[prepared.parent].index].sprite_scale;
    if (scale == v(0)) {
        scale = v(1);
        ctx.world.entities[prepared.parent].skills[10] = scale.to_float();
    }
    entity.skills[18] = prepared.trunk_min_scale.to_float();
    const auto sprite_scale = scale * v(ctx.world.source_profile.mtq == 2 ? 1 : 2);
    auto scales = physics::body_source(body.scale);
    scales.x = scales.y = sprite_scale;
    body.scale = physics::native(scales);
    entity.scale[0] = entity.scale[1] = sprite_scale.to_float();
    auto position = physics::body_source(body.position);
    position.z += prepared.height;
    publish_position(entity, body, position);
    ++visual.revision;
    state.initialized = true;
    return false;
}

bool WorldLeaves::dispatch_entity(GameContext& ctx, FrameTiming timing, std::uint32_t id) {
    spawned_entities_.clear();
    const auto& process = entity_process_.at(id);
    if (process.index == unbound) throw std::logic_error("Unbound source leaf process");
    if (!active_) return false;
    if (ctx.state.freeze_mode != v(0)) return true;
    return process.leaf ? dispatch_leaf(ctx, timing, process.index) : dispatch_tree(ctx, timing, process.index);
}

void WorldLeaves::update(GameContext& ctx, FrameTiming timing) {
    if (!active_ || ctx.state.freeze_mode != v(0) || timing.frame_index == last_frame_) return;
    last_frame_ = timing.frame_index;
    next_queue_.clear();
    for (const auto id : queue_) {
        const bool retained = dispatch_entity(ctx, timing, id);
        for (const auto child : spawned_entities_) next_queue_.push_back(child);
        if (retained) next_queue_.push_back(id);
    }
    queue_.swap(next_queue_);
}

void WorldLeaves::teardown(GameContext& ctx) {
    if (!active_) return;
    active_ = false;
    queue_.clear();
    next_queue_.clear();
    spawned_entities_.clear();
    for (std::size_t index = 0; index < startup_.size(); ++index) {
        const auto id = startup_[index].entity;
        ctx.world.entities[id].native_prepared_inactive = true;
        flags(ctx.world.entities[id], invisible);
        ctx.visuals[id].invisible = true;
        ++ctx.visuals[id].revision;
        leaf_state_[index] = {};
    }
    for (auto& tree : tree_state_) tree = {};
}

WorldLeaves::Coverage WorldLeaves::coverage() const {
    Coverage result;
    result.trees = trees_.size();
    result.prepared = startup_.size();
    result.queued = active_;
    for (const auto& tree : tree_state_) result.pending_trees += tree.stage != TreeStage::complete;
    for (const auto& leaf : leaf_state_) {
        result.born += leaf.born;
        result.initialized += leaf.initialized;
        result.pending_callbacks += leaf.born && !leaf.initialized;
    }
    return result;
}
} // namespace c4::game
