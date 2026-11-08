#include "game/GameState.hpp"
#include "game/WorldLeaves.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <tuple>
#include <utility>
#include <vector>

namespace {
using namespace c4;
using namespace game;
using physics::body_source;
using physics::v;

struct Recipe {
    std::string_view action, image;
    std::size_t trees, children;
    Var scale, height, rotation;
};
// canli_agaclar.wdl:115-397; bogazici action counts independently recorded in
// work/reports/world-format-evidence.json. Model names do not select recipes.
const std::array<Recipe, 9> recipes{{
    {"can_agac1", "tl11_ashLeaveSprite", 42, 11, v(.9), v(16), v(360)},
    {"can_agac2", "tl11_birchLeaveSprite", 7, 5, v(.6), v(16), v(360)},
    {"can_agac3", "tl11_birchLeaveSprite", 35, 8, v(.6), v(16), v(360)},
    {"can_agac4", "tl11_busch1LeaveSprite", 18, 8, v(.6), v(14), v(360)},
    {"can_agac5", "tl11_busch2LeaveSprite", 3, 7, v(.65), v(16), v(360)},
    {"can_agac6", "tl11_beechLeaveSprite", 9, 7, v(.85), v(16), v(360)},
    {"can_agac7", "tl11_platanLeaveSprite", 21, 12, v(.8), v(4), v(60)},
    {"can_agac8", "tl11_cloneLeaveSprite", 16, 11, v(1.1), v(0), v(360)},
    {"can_agac9", "tl11_elmeaveSprite", 60, 12, v(1.1), v(0), v(360)}
}};
std::uint32_t flags(const assets::WorldEntity& entity) {
    return (entity.flags | entity.source_flags_set) & ~entity.source_flags_clear;
}
void equal(physics::Vec3 a, physics::Vec3 b) {
    assert(a.x == b.x && a.y == b.y && a.z == b.z);
}
void equal(ScriptVec a, ScriptVec b) {
    assert(a.x.raw == b.x.raw && a.y.raw == b.y.raw && a.z.raw == b.z.raw);
}
void position(const assets::WorldEntity& entity, const physics::Body& body, ScriptVec expected) {
    assert(entity.source_position_words);
    const std::array<std::int32_t, 3> words{expected.x.raw, expected.y.raw, expected.z.raw};
    assert(*entity.source_position_words == words);
    equal(body_source(body.position), expected); // Never narrow published raw body properties through source().
}

void leaves(const assets::WorldScene& original, const std::filesystem::path& root) {
    auto world = original;
    const auto mtq = world.source_profile.mtq;
    SharedState state;
    state.rng_state = 0x13579bdu;
    state.rng_calls = 17;
    const auto menu_rng = state;
    std::vector<VisualState> visuals;
    auto ids = bootstrap_game(world, root, state, visuals); // Before PhysicsWorld borrows scene storage.
    assert(state.phase == GamePhase::menu);
    assert(state.rng_state == menu_rng.rng_state && state.rng_calls == menu_rng.rng_calls);
    assert(state.world_leaf_startup.size() == 2153);
    assert(world.entities.size() == original.entities.size() + 2153);
    assert(visuals.size() == world.entities.size());
    const auto material_snapshot = [&] {
        std::vector<std::pair<std::string_view, bool>> snapshot;
        snapshot.reserve(visuals.size());
        for (const auto& visual : visuals)
            snapshot.emplace_back(visual.material_identity, visual.material_identity_known);
        return snapshot;
    };
    const auto prepared_materials = material_snapshot();

    std::array<std::string_view, recipes.size()> aliases{};
    for (std::size_t i = 0; i < recipes.size(); ++i) aliases[i] = recipes[i].image;
    const auto images = assets::resolve_resources(root / "C4.wrs", aliases, world.source_profile);
    physics::PhysicsWorld physics(world);
    audio::Audio audio(48000, audio::Mode::Offline);
    SoundCache sounds;
    GameContext ctx{world, physics, audio, state, ids, visuals, root, sounds};
    WorldLeaves actors(ctx);
    assert(actors.bound_entities().size() == 211 && actors.leaf_entities().size() == 2153);

    std::array<std::size_t, recipes.size()> counts{};
    std::size_t ordinal = 0, tree_ordinal = 0;
    for (EntityId parent = 0; parent < original.entities.size(); ++parent) {
        const auto& trunk = original.entities[parent];
        const auto found = std::find_if(recipes.begin(), recipes.end(), [&](const Recipe& recipe) {
            return recipe.action == trunk.action;
        });
        if (found == recipes.end()) continue;
        const auto recipe_index = static_cast<std::size_t>(found - recipes.begin());
        ++counts[recipe_index];
        assert(actors.bound_entities()[tree_ordinal++] == parent);
        assert((std::pair{visuals[parent].material_identity, visuals[parent].material_identity_known} == prepared_materials[parent]));
        for (std::size_t vertex = 0; vertex < found->children; ++vertex, ++ordinal) {
            const auto& seed = state.world_leaf_startup[ordinal];
            const auto& leaf = world.entities[seed.entity];
            assert(seed.entity == original.entities.size() + ordinal && seed.parent == parent);
            assert(actors.leaf_entities()[ordinal] == seed.entity);
            assert(seed.height.raw == (found->height * v(trunk.scale[0])).raw);
            assert(seed.scale.raw == (found->scale * v(trunk.scale[0])).raw);
            assert(seed.rotation_range.raw == found->rotation.raw);
            assert(seed.trunk_min_scale.raw == v(trunk.skills[18]).raw);
            const auto sdk_vertex = physics.vec_for_vertex(parent, static_cast<std::uint32_t>(vertex + 1));
            equal(seed.exact_spawn_position, body_source(sdk_vertex));
            assert(leaf.object_index == no_entity && leaf.action == "tl1_LeaveInit");
            assert(leaf.attached_entity == 0);
            assert((std::pair{visuals[seed.entity].material_identity, visuals[seed.entity].material_identity_known} == prepared_materials[seed.entity]));
            const auto& binding = world.models.at(leaf.model_index);
            assert(binding.resource == images[recipe_index]);
            assert(binding.sprite.source_format == assets::ImageSourceFormat::Dds);
            assert(binding.sprite.width != 0 && binding.sprite.height != 0 && !binding.sprite.rgba.empty());
            assert((flags(leaf) & 0x100u) != 0 && visuals[seed.entity].invisible);
            assert(leaf.native_prepared_inactive);
            assert((leaf.source_flags_set & (0x200u | 0x2000u | 0x400000u)) == 0); // No LeafInit during Menu preparation.
            auto prepared_position = seed.exact_spawn_position;
            prepared_position.z += seed.height; // Inert preparation retains the final exact raw words.
            position(leaf, physics.body(seed.entity), prepared_position);
        }
    }
    assert(ordinal == 2153 && tree_ordinal == 211);
    for (std::size_t i = 0; i < counts.size(); ++i) assert(counts[i] == recipes[i].trees);

    const auto* entity_storage = world.entities.data();
    const auto* model_storage = world.models.data();
    const auto* visual_storage = visuals.data();
    const auto* startup_storage = state.world_leaf_startup.data();
    const auto entity_count = world.entities.size(), model_count = world.models.size();
    using ModelIdentity = std::tuple<std::filesystem::path, const assets::Model*, const assets::Model*, const std::uint8_t*>;
    std::vector<ModelIdentity> model_identity;
    for (const auto& model : world.models)
        model_identity.emplace_back(model.resource, model.model.get(), model.morphed_model.get(), model.sprite.rgba.data());
    std::vector<const physics::Body*> body_identity;
    for (EntityId id = 0; id < world.entities.size(); ++id) body_identity.push_back(&physics.body(id));
    const auto stable = [&] {
        assert(world.entities.size() == entity_count && world.entities.data() == entity_storage);
        assert(world.models.size() == model_count && world.models.data() == model_storage);
        assert(visuals.data() == visual_storage && state.world_leaf_startup.data() == startup_storage);
        assert(physics.size() == entity_count && sounds.empty());
        for (EntityId id = 0; id < world.entities.size(); ++id) assert(&physics.body(id) == body_identity[id]);
        for (EntityId id = 0; id < original.entities.size(); ++id) {
            const auto& before = original.entities[id];
            const auto& after = world.entities[id];
            assert(after.object_index == before.object_index && after.name == before.name);
            assert(after.action == before.action && after.resource == before.resource && after.model_index == before.model_index);
        }
        for (std::size_t i = 0; i < original.models.size(); ++i) {
            assert(world.models[i].resource == original.models[i].resource);
            assert(world.models[i].model == original.models[i].model);
            assert(world.models[i].morphed_resource == original.models[i].morphed_resource);
            assert(world.models[i].morphed_model == original.models[i].morphed_model);
        }
        for (std::size_t i = 0; i < model_count; ++i) {
            const auto& model = world.models[i];
            assert((ModelIdentity{model.resource, model.model.get(), model.morphed_model.get(), model.sprite.rgba.data()} == model_identity[i]));
        }
    };
    FrameTiming timing;
    timing.originaltime_step_ticks = v(64);
    timing.originaltime_frame_ticks = v(64);
    const auto advance = [&] { ++timing.frame_index; actors.update(ctx, timing); stable(); };
    const auto coverage = [&](bool queued, std::size_t pending, std::size_t born, std::size_t initialized) {
        const auto actual = actors.coverage();
        assert(actual.trees == 211 && actual.prepared == 2153 && actual.queued == queued);
        assert(actual.pending_trees == pending && actual.born == born && actual.initialized == initialized);
        assert(actual.pending_callbacks == born - initialized);
    };
    const auto freeze = [&] {
        const auto before = actors.coverage();
        const auto rng_state = state.rng_state;
        const auto rng_calls = state.rng_calls;
        std::vector<physics::Body> bodies;
        std::vector<std::uint64_t> revisions;
        for (const auto& seed : state.world_leaf_startup) {
            bodies.push_back(physics.body(seed.entity));
            revisions.push_back(visuals[seed.entity].revision);
        }
        for (int mode : {1, 2}) {
            state.freeze_mode = v(mode);
            for (int frame = 0; frame < 3; ++frame) {
                advance();
                coverage(before.queued, before.pending_trees, before.born, before.initialized);
                assert(state.rng_state == rng_state && state.rng_calls == rng_calls);
                for (std::size_t i = 0; i < bodies.size(); ++i) {
                    const auto id = state.world_leaf_startup[i].entity;
                    equal(physics.body(id).position, bodies[i].position);
                    equal(physics.body(id).angles, bodies[i].angles);
                    equal(physics.body(id).scale, bodies[i].scale);
                    assert(visuals[id].revision == revisions[i]);
                }
            }
        }
        state.freeze_mode = v(0);
    };

    stable();
    coverage(false, 211, 0, 0);
    advance(); // A bound component alone must remain inert in Menu.
    assert(state.rng_state == menu_rng.rng_state && state.rng_calls == menu_rng.rng_calls);
    coverage(false, 211, 0, 0);
    actors.initialize(ctx, timing);
    actors.update(ctx, timing); // Enqueue does not enter source code on the same scheduler pass.
    coverage(true, 211, 0, 0);
    freeze();
    advance(); // First tree entry, then wait(1), regardless of the large TIME_STEP above.
    coverage(true, 211, 0, 0);
    for (const auto parent : actors.bound_entities())
        assert((std::pair{visuals[parent].material_identity, visuals[parent].material_identity_known} == prepared_materials[parent]));
    actors.initialize(ctx, timing); // Retained initialize must not restart the entry/wait state.
    actors.update(ctx, timing);
    freeze();
    timing.originaltime_step_ticks = v(0);
    timing.originaltime_frame_ticks = v(0);
    advance(); // wait(1) resumes on a later frame even at zero source TIME.
    coverage(true, 0, 2153, 0);
    assert(state.rng_state == menu_rng.rng_state && state.rng_calls == menu_rng.rng_calls);
    for (const auto parent : actors.bound_entities())
        assert(visuals[parent].material_identity_known && visuals[parent].material_identity == "Mtl_agaclar");
    for (const auto& seed : state.world_leaf_startup) {
        const auto& leaf = world.entities[seed.entity];
        const auto& body = physics.body(seed.entity);
        position(leaf, body, seed.exact_spawn_position); // Height is deferred to LeafInit, not ent_create.
        assert(flags(leaf) == 0 && !body.flags.passable && !visuals[seed.entity].invisible);
        assert(!leaf.native_prepared_inactive);
        equal(body.scale, {1, 1, 1});
        equal(body.angles, {});
        // Proven ENTITY constructor1003c2d0 defaults, not guessed sprite defaults.
        assert(leaf.scale == (std::array<float, 3>{1, 1, 1}));
        assert(leaf.ambient == 0 && leaf.albedo == 50 && leaf.alpha_percent == 50);
        assert(visuals[seed.entity].alpha == v(50) && visuals[seed.entity].frame == v(1));
        assert(visuals[seed.entity].next_frame == v(0) && !visuals[seed.entity].transparent);
        assert(leaf.skills[18] == 0);
        assert(visuals[seed.entity].material_identity_known == leaf.source_initial_material_known);
        const auto initial_material = leaf.source_initial_material_known
            ? leaf.source_initial_material_identity : std::string_view{};
        assert(visuals[seed.entity].material_identity == initial_material);
    }
    const auto birth_materials = material_snapshot(); // Resource-bound birth baseline need not be universally unknown.
    actors.initialize(ctx, timing);
    actors.update(ctx, timing); // Children are pending until a separate later pass.
    coverage(true, 0, 2153, 0);
    freeze();

    auto expected_rng = state;
    std::vector<Var> rolls;
    for (const auto& seed : state.world_leaf_startup) rolls.push_back(source_random(expected_rng, seed.rotation_range));
    advance();
    coverage(true, 0, 2153, 2153);
    assert(state.rng_calls == menu_rng.rng_calls + 2153);
    assert(state.rng_state == expected_rng.rng_state && state.rng_calls == expected_rng.rng_calls);
    for (std::size_t i = 0; i < state.world_leaf_startup.size(); ++i) {
        const auto& seed = state.world_leaf_startup[i];
        const auto& leaf = world.entities[seed.entity];
        const auto& body = physics.body(seed.entity);
        auto final_position = seed.exact_spawn_position;
        final_position.z += seed.height;
        position(leaf, body, final_position);
        const auto roll = body_source(body.angles).z;
        assert(roll.raw == rolls[i].raw && roll >= v(0) && roll < seed.rotation_range);
        assert(v(leaf.angles[2]).raw == rolls[i].raw);
        const auto scale = (seed.scale == v(0) ? v(1) : seed.scale) * v(mtq == 2 ? 1 : 2);
        equal(body_source(body.scale), {scale, scale, v(1)});
        assert(v(leaf.scale[0]).raw == scale.raw && v(leaf.scale[1]).raw == scale.raw && leaf.scale[2] == 1);
        assert(v(leaf.skills[18]).raw == seed.trunk_min_scale.raw);
        assert(leaf.attached_entity == 0);
        assert((std::pair{visuals[seed.entity].material_identity, visuals[seed.entity].material_identity_known} == birth_materials[seed.entity]));
        assert(flags(leaf) == (0x200u | 0x2000u | 0x400000u));
        assert((leaf.source_flags_clear & (0x400u | 0x1000u)) == (0x400u | 0x1000u));
        assert(body.flags.passable && !visuals[seed.entity].invisible && !visuals[seed.entity].transparent);
        assert(!leaf.native_prepared_inactive);
        assert(leaf.ambient == 50 && leaf.albedo == 50 && leaf.alpha_percent == 100);
        assert(visuals[seed.entity].alpha == v(100));
    }
    freeze();
    actors.initialize(ctx, timing); // Retained-world reentry cannot reroll finite callbacks.
    advance();
    advance();
    coverage(true, 0, 2153, 2153);
    assert(state.rng_state == expected_rng.rng_state && state.rng_calls == expected_rng.rng_calls);
    for (std::size_t i = 0; i < state.world_leaf_startup.size(); ++i) {
        const auto& seed = state.world_leaf_startup[i];
        auto final_position = seed.exact_spawn_position;
        final_position.z += seed.height;
        position(world.entities[seed.entity], physics.body(seed.entity), final_position);
        assert(body_source(physics.body(seed.entity).angles).z.raw == rolls[i].raw);
    }

    actors.teardown(ctx);
    stable();
    coverage(false, 211, 0, 0);
    for (const auto& seed : state.world_leaf_startup) {
        assert((flags(world.entities[seed.entity]) & 0x100u) != 0 && visuals[seed.entity].invisible);
        assert(world.entities[seed.entity].native_prepared_inactive);
    }
    advance();
    assert(state.rng_state == expected_rng.rng_state && state.rng_calls == expected_rng.rng_calls);
    actors.initialize(ctx, timing); // Fresh logical births reuse every prewarmed helper and cache.
    advance();
    coverage(true, 211, 0, 0);
    advance();
    coverage(true, 0, 2153, 0);
    assert(state.rng_state == expected_rng.rng_state && state.rng_calls == expected_rng.rng_calls);
    actors.teardown(ctx);
    stable();
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: test_world_leaves ORIGINAL_ASSET_ROOT\n"; return 2; }
    const std::filesystem::path root = argv[1];
    constexpr std::array<std::string_view, 1> names{"bogazici.wmb"};
    for (std::uint8_t mtq : {0, 1, 2}) {
        auto profile = assets::original_source_profile;
        profile.mtq = mtq;
        const auto resource = assets::resolve_resources(root / "C4.wrs", names, profile).at(0);
        const auto original = assets::load_world(resource, profile);
        leaves(original, root);
    }
}
