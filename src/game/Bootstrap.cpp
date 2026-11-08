#include "GameState.hpp"
#include "WorldActors.hpp"
#include "WorldLeaves.hpp"
#include "physics/NativeNumeric.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace c4::game {
namespace {
constexpr std::uint32_t invisible = 0x100u;
constexpr std::uint32_t passable = 0x200u;
constexpr std::uint32_t transparent = 0x400u;
constexpr std::uint32_t polygon = 0x04000000u;
const physics::ScriptVec spawn_position{physics::v(27685.279), physics::v(-32348.588), physics::v(-53.770)};
const physics::ScriptVec spawn_angles{physics::v(79.985), physics::v(1.228), physics::v(-.038)};

struct ComponentBinding {
    std::string_view action;
    EntityId ComponentIds::*member;
    std::uint32_t set, clear;
    double alpha{-1};
};
// arac_fizik.wdl:41-283,703-743; ghost_car.wdl:67-83.
constexpr ComponentBinding components[] = {
    {"aracgovde", &ComponentIds::p_aracgovde, invisible, polygon},
    {"arac_cam_target", &ComponentIds::p_camera_target, passable | invisible, 0},
    {"arac_govde_cam", &ComponentIds::p_arac_cam, passable | transparent, 0, 70},
    {"arac_far_govde", &ComponentIds::p_far_govde, passable, 0},
    {"arac_far_cam", &ComponentIds::p_far_cam, passable | transparent, 0, 55},
    {"arac_on_takim", &ComponentIds::p_arac_ontakim, passable, 0},
    {"arac_ic_koltuk", &ComponentIds::p_arac_koltuk, passable, 0},
    {"arac_kaporta", &ComponentIds::p_arac_kaporta, passable, 0},
    {"arac_direksiyon", &ComponentIds::p_arac_direx, passable, 0},
    {"arac_ayna", &ComponentIds::p_arac_ayna, passable, 0},
    {"arac_tekerler", &ComponentIds::p_arac_tekerler, passable | invisible, 0},
    {"arac_arkafar", &ComponentIds::p_arac_arkafar, passable | invisible, 0},
    {"arac_arkaglow", &ComponentIds::p_arac_arkaglow, passable | invisible | transparent, 0, 35},
    {"fsolteker", &ComponentIds::p_fsolteker, passable, 0},
    {"fsagteker", &ComponentIds::p_fsagteker, passable, 0},
    {"bsolteker", &ComponentIds::p_bsolteker, passable, 0},
    {"bsagteker", &ComponentIds::p_bsagteker, passable, 0},
    {"arac_golge", &ComponentIds::p_arac_golge, passable, 0},
    {"arac_ic_camlar", &ComponentIds::p_ic_camlar, passable | invisible, 0},
    {"arac_ic_turbo", &ComponentIds::p_ic_turbo, passable | invisible, 0},
    {"arac_ic_panel", &ComponentIds::p_ic_panel, passable | invisible, 0},
    {"arac_ic_tavan", &ComponentIds::p_ic_tavan, passable | invisible, 0},
    {"arac_ic_simit", &ComponentIds::p_ic_simit, passable | invisible, 0},
    {"arac_ic_rakam0", &ComponentIds::p_ic_rakam0, passable | invisible, 0},
    {"arac_ic_rakam1", &ComponentIds::p_ic_rakam1, passable | invisible, 0},
    {"arac_ic_rakam2", &ComponentIds::p_ic_rakam2, passable | invisible, 0},
    {"robot_teker", &ComponentIds::p_robot_teker, passable | invisible, 0},
    {"robot_mekanik", &ComponentIds::p_robot_mekanik, passable | invisible, 0},
    {"robot_farcam", &ComponentIds::p_robot_farcam, passable | invisible | transparent, 0, 55},
    {"robot_fargovde", &ComponentIds::p_robot_fargovde, passable | invisible, 0},
    {"robot_camlar", &ComponentIds::p_robot_camlar, passable | invisible | transparent, 0, 70},
    {"robot_kaporta", &ComponentIds::p_robot_kaporta, passable | invisible, 0},
    {"robot_golge", &ComponentIds::p_robot_golge, passable | invisible, 0},
    {"c4_ghost_arac", &ComponentIds::p_ghost_arac, passable | invisible | transparent, 0, 0},
    {"c4_ghost_robo", &ComponentIds::p_ghost_robo, passable | invisible | transparent, 0, 0},
    {"GMA_zamanlama", &ComponentIds::p_zamanlama, passable, 0},
};

[[noreturn]] void fail(std::string_view reason, std::string_view action) {
    throw std::runtime_error("Original game bootstrap: " + std::string(reason) + " '" + std::string(action) + "'");
}
const assets::Model& model(const assets::WorldScene& world, EntityId id) {
    const auto& entity = world.entities.at(id);
    if (entity.model_index == assets::no_world_model || entity.model_index >= world.models.size())
        fail("missing original model for", entity.action);
    const auto& binding = world.models[entity.model_index];
    const auto& geometry = binding.morphed_model ? binding.morphed_model : binding.model;
    if (!geometry || geometry->groups.empty()) fail("missing original geometry for", entity.action);
    return *geometry;
}
void require_morph(const assets::WorldScene& world, EntityId id, const std::filesystem::path& resource) {
    const auto& entity = world.entities.at(id);
    model(world, id);
    const auto& binding = world.models.at(entity.model_index);
    if (!binding.morphed_model || binding.morphed_resource != resource)
        fail("missing source-selected morph for", entity.action);
}
void source_flags(assets::WorldEntity& entity, std::uint32_t set, std::uint32_t clear = 0) {
    entity.source_flags_set = (entity.source_flags_set | set) & ~clear;
    entity.source_flags_clear = (entity.source_flags_clear | clear) & ~set;
}
unsigned path_index(std::string_view action) {
    constexpr std::string_view prefix = "zipla_path_";
    if (!action.starts_with(prefix) || action.size() != prefix.size() + 2) return 0;
    const char tens = action[prefix.size()], ones = action[prefix.size() + 1];
    if (tens < '0' || tens > '9' || ones < '0' || ones > '9') return 0;
    const unsigned index = unsigned(tens - '0') * 10 + unsigned(ones - '0');
    return index < 24 ? index : 0;
}

struct LeafBinding {
    std::string_view action;
    unsigned image, count;
    physics::Var scale, height, rotation;
};
// Resolve the original source variable names once, using the scene's actual
// compile selectors rather than a native renderer quality preset.
constexpr std::size_t leaf_image_count = 8;
constexpr std::array<std::string_view, 13> bootstrap_aliases{
    "tl11_ashLeaveSprite", "tl11_birchLeaveSprite", "tl11_busch1LeaveSprite", "tl11_busch2LeaveSprite",
    "tl11_beechLeaveSprite", "tl11_platanLeaveSprite", "tl11_cloneLeaveSprite", "tl11_elmeaveSprite",
    "MTM_zamanlama", "C_iconcam_model", "MTM_turbo_green", "MTM_turbo_blue", "MTM_turbo_red"
};
enum SourceResource : unsigned {
    timing_resource = 8, interior_glass_resource, turbo_green_resource, turbo_blue_resource, turbo_red_resource
};
const LeafBinding leaves[] = {
    {"can_agac1", 0, 11, physics::v(.9), physics::v(16), physics::v(360)},
    {"can_agac2", 1, 5, physics::v(.6), physics::v(16), physics::v(360)},
    {"can_agac3", 1, 8, physics::v(.6), physics::v(16), physics::v(360)},
    {"can_agac4", 2, 8, physics::v(.6), physics::v(14), physics::v(360)},
    {"can_agac5", 3, 7, physics::v(.65), physics::v(16), physics::v(360)},
    {"can_agac6", 4, 7, physics::v(.85), physics::v(16), physics::v(360)},
    {"can_agac7", 5, 12, physics::v(.8), physics::v(4), physics::v(60)},
    {"can_agac8", 6, 11, physics::v(1.1), physics::v(0), physics::v(360)},
    {"can_agac9", 7, 12, physics::v(1.1), physics::v(0), physics::v(360)},
};

void create_source_leaves(assets::WorldScene& world, SharedState& state,
                          std::span<const std::filesystem::path> resources) {
    using physics::v;
    using physics::number;
    // canli_agaclar.wdl:59,76: MTQ0/1 use multiplier 2, MTQ2 uses 1.
    const auto image_scale = v(world.source_profile.mtq == 2 ? 1 : 2);
    const auto original_count = world.entities.size();
    state.world_leaf_startup.clear();
    std::size_t count = 0;
    std::array<bool, leaf_image_count> used{};
    for (const auto& entity : world.entities)
        for (const auto& binding : leaves)
            if (entity.action == binding.action) { count += binding.count; used[binding.image] = true; }
    if (!count) return;
    std::array<std::uint32_t, leaf_image_count> image_models{};
    std::array<std::string, leaf_image_count> image_names{};
    std::array<assets::SourceMaterialIdentity, leaf_image_count> image_materials{};
    for (unsigned index = 0; index < used.size(); ++index) {
        if (!used[index]) continue;
        const auto& resource = resources[index];
        const auto qualified = resource.string();
        const auto separator = qualified.find("::");
        image_names[index] = separator == std::string::npos ? resource.filename().string() : qualified.substr(separator + 2);
        const auto found = std::find_if(world.models.begin(), world.models.end(),
            [&](const assets::WorldModel& item) { return item.resource == resource && item.sprite.width != 0; });
        if (found != world.models.end()) image_models[index] = static_cast<std::uint32_t>(found - world.models.begin());
        else {
            assets::WorldModel image;
            image.resource = resource;
            image.sprite_gpu = assets::load_gpu_image(resource, image.sprite, {true, 16, 32});
            image_models[index] = static_cast<std::uint32_t>(world.models.size());
            world.models.push_back(std::move(image));
        }
        image_materials[index] = assets::source_fresh_material_identity(
            world.models[image_models[index]], world.source_profile);
    }
    // Keep original compact entity IDs stable and do not invalidate the creating parent.
    world.entities.reserve(original_count + count);
    state.world_leaf_startup.reserve(count);
    for (EntityId id = 0; id < original_count; ++id) {
        auto& parent = world.entities[id];
        const auto found = std::find_if(std::begin(leaves), std::end(leaves),
            [&](const LeafBinding& item) { return item.action == parent.action; });
        if (found == std::end(leaves)) continue;
        const auto& binding = *found;
        const auto& group = model(world, id).groups.front();
        if (group.source_vertices.size() < binding.count) fail("missing original leaf attachment vertices for", parent.action);
        const auto parent_scale = v(parent.scale[0]);
        const auto sprite_scale = binding.scale * parent_scale;
        const auto height = binding.height * parent_scale;
        const auto position = physics::native(physics::source({parent.position[0], parent.position[1], parent.position[2]}));
        const auto angles = physics::native(physics::source({parent.angles[0], parent.angles[1], parent.angles[2]}));
        const auto scale = physics::native(physics::source({parent.scale[0], parent.scale[1], parent.scale[2]}));
        for (unsigned vertex = 0; vertex < binding.count; ++vertex) {
            const auto original = assets::sample_source_vertex(group, {}, vertex);
            const auto exact_spawn = physics::source(physics::model_vertex_position(
                {original.x, original.y, original.z}, position, angles, scale));
            auto attachment = exact_spawn;
            attachment.z += height;
            const auto placed = physics::native(attachment);
            assets::WorldEntity leaf;
            leaf.object_index = no_entity; // ent_create has no WMB disk object record.
            leaf.native_prepared_inactive = true;
            leaf.model_index = image_models[binding.image];
            leaf.resource = image_names[binding.image];
            // Creation recipes only: current material is published at actual birth.
            leaf.source_initial_material_identity = image_materials[binding.image].identity;
            leaf.source_initial_material_known = image_materials[binding.image].known;
            leaf.source_previous_material_known = true; // Normal constructor's prior MAT is NULL.
            leaf.action = "tl1_LeaveInit";
            leaf.position = {static_cast<float>(placed.x), static_cast<float>(placed.y), static_cast<float>(placed.z)};
            leaf.source_position_words = std::array<std::int32_t, 3>{attachment.x.raw, attachment.y.raw, attachment.z.raw};
            // This slot is an inert cache, not a live source entity. Birth and
            // tl1_LeaveInit alone publish constructor defaults and consume random.
            const auto leaf_scale = (sprite_scale == v(0) ? v(1) : sprite_scale) * image_scale;
            leaf.scale = {static_cast<float>(number(leaf_scale)), static_cast<float>(number(leaf_scale)), 1};
            leaf.albedo = 50; // Vendor entity default, not the trunk material.
            source_flags(leaf, invisible);
            state.world_leaf_startup.push_back({
                static_cast<EntityId>(world.entities.size()), id, exact_spawn,
                height, sprite_scale, binding.rotation, v(parent.skills[18])
            });
            world.entities.push_back(std::move(leaf));
        }
    }
}
} // namespace

ComponentIds bootstrap_game(assets::WorldScene& world, const std::filesystem::path& asset_root,
                            SharedState& state, std::vector<VisualState>& visuals) {
    using physics::v;
    using physics::number;
    const auto resources = assets::resolve_resources(asset_root / "C4.wrs", bootstrap_aliases, world.source_profile);
    // C4.wdl:217,304-305: the two startup predicates are independent and ordered.
    state.particle_count = v(1);
    if (world.source_profile.shq == 0 && world.source_profile.cgm == 0) state.particle_count = v(0);
    if (world.source_profile.bloom == 1 || world.source_profile.shq == 2 || world.source_profile.mtq == 2)
        state.particle_count = v(2);
    state.d3d_anisotropy = v(world.source_profile.ano == 1 ? 2 : 0);
    state.bloom_aktif = v(world.source_profile.bloom); // Source compile flag, not native HDR visibility.
    const std::array<physics::Var, 4> clip_far{v(55000), v(112500), v(280000), v(280000)};
    state.camera_clip_far = clip_far.at(world.source_profile.cgm);
    create_source_leaves(world, state, resources);
    ComponentIds ids;
    visuals.assign(world.entities.size(), VisualState{});
    for (EntityId id = 0; id < world.entities.size(); ++id) {
        auto& entity = world.entities[id];
        auto& visual = visuals[id];
        visual.alpha = v(entity.alpha_percent);
        for (const auto& component : components) {
            if (entity.action != component.action) continue;
            if (ids.*component.member != no_entity) fail("duplicate required component", entity.action);
            model(world, id);
            ids.*component.member = id;
            source_flags(entity, component.set, component.clear);
            if (component.alpha >= 0) {
                visual.alpha = v(component.alpha);
                entity.alpha_percent = static_cast<float>(number(visual.alpha));
            }
        }
        if (const auto index = path_index(entity.action)) {
            if (ids.path_ent_dizi[index] != no_entity) fail("duplicate required path", entity.action);
            const auto& geometry = model(world, id);
            if (geometry.groups.front().source_vertices.size() < 4) fail("missing original path vertices for", entity.action);
            ids.path_ent_dizi[index] = id;
            source_flags(entity, passable | invisible);
            entity.position[2] = static_cast<float>(number(index == 2 || (index >= 17 && index <= 19)
                ? v(-1362) : v(entity.position[2]) - v(19)));
        }
        if (entity.action == "GMA_turbo") {
            model(world, id);
            // c4_effects.wdl:708-710: independent ordered flag tests, last one wins.
            std::optional<SourceResource> morph;
            if (entity.flags & 1u) { entity.skills[0] = 1; morph = turbo_green_resource; }
            if (entity.flags & 2u) { entity.skills[0] = 2; morph = turbo_blue_resource; }
            if (entity.flags & 4u) { entity.skills[0] = 3; morph = turbo_red_resource; }
            if (morph) require_morph(world, id, resources[*morph]);
            source_flags(entity, passable, invisible);
            ids.pickups.push_back(id);
        }
        const auto flags = (entity.flags | entity.source_flags_set) & ~entity.source_flags_clear;
        visual.invisible = (flags & invisible) != 0;
        visual.transparent = (flags & transparent) != 0;
    }
    for (const auto& component : components)
        if (ids.*component.member == no_entity) fail("missing required component", component.action);
    for (unsigned index = 1; index < ids.path_ent_dizi.size(); ++index)
        if (ids.path_ent_dizi[index] == no_entity) fail("missing required path", "zipla_path_" + std::to_string(index));
    if (ids.pickups.empty()) fail("missing required component", "GMA_turbo");
    require_morph(world, ids.p_zamanlama, resources[timing_resource]);
    require_morph(world, ids.p_ic_camlar, resources[interior_glass_resource]);
    if (model(world, ids.p_aracgovde).groups.front().source_vertices.size() < 12)
        fail("missing original collision body vertices for", "aracgovde");
    if (model(world, ids.p_camera_target).groups.front().source_vertices.size() < 4)
        fail("missing original race target vertices for", "arac_cam_target");
    for (const auto id : {ids.p_aracgovde, ids.p_camera_target}) {
        const auto position = physics::native(spawn_position);
        const auto angles = physics::native(spawn_angles);
        world.entities[id].position = {static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z)};
        world.entities[id].angles = {static_cast<float>(angles.x), static_cast<float>(angles.y), static_cast<float>(angles.z)};
    }
    state.selected_path = no_entity; // Runtime skill70 is not one of the twenty WMB disk skills.
    prepare_world_actors(world, state, visuals);
    return ids;
}

void initialize_game_physics(GameContext& context) {
    using physics::v;
    using physics::number;
    auto& world = context.physics;
    world.move_friction = number(v(0));
    world.move_min_z = number(v(.4));
    // ana_menu.wdl:3275-3291 and arac_fizik.wdl:709-724 load-time setup.
    // Preparing initial data does not advance wait(1), frames, or the source clock.
    world.narrow_hull = {{-90, -45, -36}, {90, 45, 30}};
    world.set_minmax(context.ids.p_aracgovde);
    auto& body = world.body(context.ids.p_aracgovde);
    body.flags.polygon = false;
    body.hull.max.z = number(v(body.hull.max.z) * v(.5));
    for (const auto id : {context.ids.p_aracgovde, context.ids.p_camera_target}) {
        auto& source_body = world.body(id);
        source_body.position = physics::native(spawn_position);
        source_body.angles = physics::native(spawn_angles);
    }
    // ENABLE_ENTITY/event is deferred to the source oyun_oynanabilir >= 100 gate.
    constexpr auto mode = physics::Mode::ignore_me | physics::Mode::ignore_passable |
        physics::Mode::ignore_content | physics::Mode::ignore_push | physics::Mode::ignore_sprites |
        physics::Mode::ignore_flag2;
    for (const auto id : context.ids.pickups) {
        auto& pickup = world.body(id);
        auto target = physics::source(pickup.position);
        target.z -= v(2000);
        const auto hit = world.trace(pickup.position, physics::native(target), mode, id);
        if (hit.trace_hit && v(hit.distance) != v(0)) pickup.position.z = number(v(hit.target.z) + v(50));
        context.world.entities[id].position[2] = static_cast<float>(pickup.position.z);
    }
}
} // namespace c4::game
