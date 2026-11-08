#include "game_render_bridge.hpp"

#include "render_scene.hpp"
#include "game/GameState.hpp"
#include "physics/NativeNumeric.hpp"
#include "rendering/MaterialEvents.hpp"
#include "rendering/SourceLighting.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace c4::rendering {
namespace {
constexpr std::uint32_t invisible = 0x100u, transparent = 0x400u;
constexpr std::array<std::string_view, 20> color_resources{
    "C_skin_gri_img", "C_skin_kirmizi_img", "C_skin_kul_img",
    "C_skin_altin_img", "C_skin_mavi_img", "C_skin_deve_img",
    "C_skin_yesil_img", "C_skin_beyaz_img", "C_skin_siyah_img",
    "C_skin_turuncu_img", "R_skin_gri_img", "R_skin_kirmizi_img",
    "R_skin_kul_img", "R_skin_altin_img", "R_skin_mavi_img",
    "R_skin_deve_img", "R_skin_yesil_img", "R_skin_beyaz_img",
    "R_skin_siyah_img", "R_skin_turuncu_img"
};
constexpr std::array<std::string_view, 6> body_materials{
    "Mtl_car3", "Mtl_kul_car", "Mtl_siyah_car",
    "Mtl_robot_kaporta", "Mtl_kul_robot", "Mtl_siyah_robot"
};
constexpr std::array<std::string_view, 5> builtin_materials{
    "mat_model", "mat_sprite", "mat_metal", "mat_terrain", "mat_unlit"
};

bool same(physics::Vec3 a, physics::Vec3 b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

std::array<float, 16> body_transform(const physics::Body& body) {
    const auto axes = physics::rotation_axes(body.angles);
    // Source 1004e240/10042a00 stores R * diag(f32 scale) coefficients
    // before 1004bd80 performs the affine point transform.
    const physics::Vec3 scale{float(body.scale.x), float(body.scale.y), float(body.scale.z)};
    const std::array<float, 16> matrix{
        float(axes[0].x * scale.x), float(axes[0].y * scale.x), float(axes[0].z * scale.x), 0,
        float(axes[1].x * scale.y), float(axes[1].y * scale.y), float(axes[1].z * scale.y), 0,
        float(axes[2].x * scale.z), float(axes[2].y * scale.z), float(axes[2].z * scale.z), 0,
        float(body.position.x), float(body.position.y), float(body.position.z), 1
    };
    if (!std::all_of(matrix.begin(), matrix.end(), [](float value) { return std::isfinite(value); }))
        throw std::runtime_error("Source game entity has a nonfinite render transform");
    return matrix;
}

int color_family(game::EntityId id, const game::ComponentIds& ids) noexcept {
    // effect_define.wdl:652-660: only these six entity-local morphskin calls.
    if (id == ids.p_arac_kaporta || id == ids.p_arac_ayna || id == ids.p_arac_direx || id == ids.p_arac_koltuk) return 0;
    if (id == ids.p_robot_kaporta || id == ids.p_robot_mekanik) return 1;
    return -1;
}


void finish_material(RenderScene& scene, Instance& instance, std::string_view name, float alpha) {
    auto& state = instance.material_state;
    state.source_profile = scene.world->source_profile;
    const auto texture_transform = state.texture_transform;
    configure_instance_material(scene, instance, name, alpha);
    state.texture_transform = texture_transform; // Same MATERIAL identity: alpha/flags do not restart its event.
    instance.diffuse = state.diffuse;
}
} // namespace

struct GameRenderBridge::Impl {
    struct Batch {
        std::uint32_t instance{};
        std::optional<std::uint32_t> original_texture;
        bool first_faces{};
        int color_family{-1};
        const std::vector<std::uint32_t>* selectable_skins{};
        // Only the two source body actors change MATERIAL with the chosen color.
        std::unique_ptr<std::array<NativeMaterialState, 3>> body_variants;
        NativeMaterialState null_material;
    };
    struct Entity {
        game::EntityId id{};
        std::vector<Batch> batches;
        std::string_view material_snapshot{}, rendered_material{};
        bool material_snapshot_known{}, rendered_material_known{};
        bool material_prior_pending{true};
        std::vector<std::uint32_t> animated_groups;
        physics::Vec3 position{}, angles{}, scale{};
        assets::AnimationSample sample{};
        std::uint64_t revision{};
        std::uint32_t flags{};
        physics::Var alpha{};
        float ambient{}, albedo{};
        int skin{}, color{-1};
        std::optional<int> palette_skin;
        bool initialized{}, color_bound{}, sprite{}, inactive{};
    };
    struct SpriteCache {
        std::array<std::int32_t, 3> authored_words{};
        std::array<float, 3> cached_angles{};
        bool qualified{}, dynamic{}, born{};
    };
    struct WaterProcess {
        // Water has no per-process locals beyond saved MY and wait(1).
        // Each count retains independent bodies, including duplicate launches.
        game::EntityId saved_my{};
        std::uint64_t waiting_count{};
    };
    struct NamedMaterial {
        NativeMaterialState state;
        WaterMaterialEventState water_event;
        std::optional<std::uint32_t> water_skin;
        std::vector<WaterProcess> water_processes;
        std::vector<game::EntityId> water_receivers;
        bool builtin{};
    };

    RenderScene& scene;
    game::GameContext& context;
    std::vector<Entity> entities;
    std::vector<SpriteCache> sprite_caches;
    std::vector<std::array<assets::SourceMaterialIdentity, 4>> null_materials;
    std::map<const assets::ModelGroup*, std::vector<std::uint32_t>> selectable_skins;
    std::array<std::uint32_t, 20> colors{};
    std::map<std::string, NamedMaterial, std::less<>> named_materials;
    assets::SourceProfile source_profile{};
    std::array<float, 16> last_view{};
    std::array<float, 16> inverse_view{};
    std::array<float, 3> last_view_position{};
    bool view_initialized{};
    std::uint64_t water_write_revision{};
    std::uint64_t source_history_epoch{1};
    const assets::WorldEntity* world_entities{};
    const assets::WorldModel* world_models{};
    const Instance* scene_instances{};
    const MeshView* scene_meshes{};
    const game::VisualState* visual_states{};
    std::size_t entity_count{}, model_count{}, instance_count{}, mesh_count{}, texture_count{};


    const std::vector<std::uint32_t>& skins(game::EntityId id, std::uint32_t group_index) {
        const auto& group = source_model_group(scene, id, group_index);
        auto [cached, inserted] = selectable_skins.try_emplace(&group);
        if (!inserted) return cached->second;
        cached->second.reserve(group.skins.size() - 1);
        for (std::uint32_t skin = 1; skin < group.skins.size(); ++skin)
            cached->second.push_back(cache_model_skin(scene, id, group_index, skin));
        return cached->second;
    }

    void cache_named_material(std::string_view name, const NativeMaterialState& binding) {
        if (name.empty() || !binding.source_material_assigned || named_materials.find(name) != named_materials.end()) return;
        auto& material = named_materials.try_emplace(std::string(name)).first->second;
        material.state = binding;
        if (!material.state.source_material_initialized)
            initialize_source_material_event_state(material.state, name);
        if (name == "mtl_denizwater") {
            material.water_skin = binding.material_skins[0];
            initialize_water_material_source_defaults(material.water_event, material.state);
            material.state.technique = MaterialTechnique::FixedFunction;
            material.state.material_skins = {};
            configure_material_state(material.state, name, 0);
            material.water_processes.resize(context.world.entities.size());
            material.water_receivers.reserve(context.world.entities.size());
            for (game::EntityId id = 0; id < material.water_processes.size(); ++id)
                material.water_processes[id].saved_my = id;
        }
    }

    NamedMaterial& named_material(std::string_view name) {
        const auto found = named_materials.find(name);
        if (found == named_materials.end())
            throw std::logic_error("Source current MATERIAL has no prepared actual named binding");
        return found->second;
    }

    static void copy_material_event_state(NativeMaterialState& binding, const NativeMaterialState& shared) {
        binding.source_event = shared.source_event;
        binding.source_event_known = shared.source_event_known;
        binding.source_material_initialized = shared.source_material_initialized;
        binding.source_flags = shared.source_flags;
        binding.source_flags_known_mask = shared.source_flags_known_mask;
        binding.texture_transform = shared.texture_transform;
        binding.skills = shared.skills;
        binding.skills_known_mask = shared.skills_known_mask;
        binding.texture_factor = shared.texture_factor;
        binding.texture_factor_known = shared.texture_factor_known;
    }

    void publish_material(std::string_view name, const NamedMaterial& shared) {
        for (auto& entity : entities) {
            for (auto& batch : entity.batches) {
                auto& instance = scene.instances[batch.instance];
                if (entity.material_snapshot_known && entity.material_snapshot == name && instance.material == name) {
                    auto& binding = instance.material_state;
                    if (binding.technique != shared.state.technique || binding.material_skins != shared.state.material_skins
                            || binding.material_skins_known_mask != shared.state.material_skins_known_mask) {
                        binding.technique = shared.state.technique;
                        binding.material_skins = shared.state.material_skins;
                        binding.material_skins_known_mask = shared.state.material_skins_known_mask;
                        finish_material(scene, instance, name, instance.source_alpha_percent);
                    }
                    copy_material_event_state(binding, shared.state);
                    ensure_source_tangents(scene, instance);
                }
                if (batch.body_variants) {
                    const auto family = entity.id == context.ids.p_arac_kaporta ? 0u : 1u;
                    for (std::size_t variant = 0; variant < 3; ++variant)
                        if (body_materials[family * 3 + variant] == name)
                            copy_material_event_state((*batch.body_variants)[variant], shared.state);
                }
            }
        }
    }

    void consume_material_source_writes() {
        const auto revision = context.state.source_water_material_write_revision;
        if (water_write_revision == revision) return;
        auto& material = named_material("mtl_denizwater");
        if (!material.water_skin)
            throw std::logic_error("Source water action has no prepared actual MTI_denz_nor bitmap");
        apply_source_water_material_action(material.state, *material.water_skin);
        const auto texture_transform = material.state.texture_transform;
        configure_material_state(material.state, "mtl_denizwater", 0);
        material.state.texture_transform = texture_transform;
        water_write_revision = revision;
        publish_material("mtl_denizwater", material);
    }

    Impl(RenderScene& value, game::GameContext& game_context)
        : scene(value), context(game_context) {
        if (!scene.world || &*scene.world != &context.world)
            throw std::invalid_argument("Game render bridge requires the exact final physics world");
        if (context.physics.size() != context.world.entities.size() || context.visuals.size() != context.world.entities.size())
            throw std::invalid_argument("Game render bridge entity, physics and visual identities differ");
        for (const auto name : builtin_materials) {
            auto& material = named_materials.try_emplace(std::string(name)).first->second;
            material.state.source_profile = context.world.source_profile;
            if (!initialize_builtin_material_state(material.state, name))
                throw std::logic_error("Source engine builtin MATERIAL definition is unavailable");
            material.builtin = true;
        }
        const auto owner = context.asset_root / "C4.wrs";
        const auto color_paths = assets::resolve_resources(owner, color_resources, context.world.source_profile);
        for (std::size_t color = 0; color < colors.size(); ++color) colors[color] = cache_texture(scene, color_paths.at(color));
        const auto materials = assets::resolve_material_resources(owner, body_materials, context.world.source_profile);
        std::array<NativeMaterialState, 6> material_bindings{};
        for (std::size_t material = 0; material < materials.size(); ++material) {
            const auto& source = materials[material];
            if (!source.definition_known) throw std::runtime_error("Source body color MATERIAL definition is unavailable");
            auto& binding = material_bindings[material];
            binding.technique = material_from_effect(source.effect.filename().string());
            binding.material_skins_known_mask = source.material_skins_known_mask;
            for (std::size_t slot = 0; slot < source.skins.size(); ++slot)
                if (!source.skins[slot].empty()) binding.material_skins[slot] = cache_texture(scene,
                    source.skins[slot], (source.material_skin_cube_mask & (1u << slot)) != 0);
        }

        entities.resize(context.world.entities.size());
        for (game::EntityId id = 0; id < entities.size(); ++id) {
            auto& entity = entities[id];
            entity.id = id;
            entity.material_snapshot = context.world.entities[id].source_previous_material_identity;
            entity.material_snapshot_known = context.world.entities[id].source_previous_material_known;
        }
        null_materials.resize(context.world.models.size());
        for (std::size_t model = 0; model < null_materials.size(); ++model)
            for (std::uint32_t flags = 0; flags < 4; ++flags)
                null_materials[model][flags] = assets::source_resource_material_identity(
                    context.world.models[model],
                    ((flags & 1u) ? 0x20000u : 0u) | ((flags & 2u) ? 0x400000u : 0u),
                    context.world.source_profile);
        sprite_caches.resize(entities.size());
        for (const auto& binding : scene.sprite_bindings) {
            const auto id = binding.source_entity;
            entities.at(id).sprite = true;
            const auto& source = context.world.entities[id];
            auto& cache = sprite_caches[id];
            cache.dynamic = source.action == "sadece_don" || source.action == "tl1_LeaveInit";
            const bool flower = source.action.empty() && (source.resource == "cicek_1.bmp"
                || source.resource == "cicek_2.bmp" || source.resource == "cicek_3.bmp"
                || source.resource == "cicek_4.bmp" || source.resource == "cicek_6.bmp");
            // The recovered ordinary cohort has no declared-ENTITY private
            // flag writers: flowers, facing helpers, fixed shadow and leaves.
            cache.qualified = cache.dynamic || flower || source.action == "sabit_tga";
            cache.born = !source.native_prepared_inactive;
            const auto authored = physics::body_source(context.physics.body(id).angles);
            cache.authored_words = {authored.x.raw, authored.y.raw, authored.z.raw};
            cache.cached_angles = {float(physics::number(authored.x)),
                float(physics::number(authored.y)), float(physics::number(authored.z))};
        }
        for (std::uint32_t index = 0; index < scene.instances.size(); ++index) {
            auto& instance = scene.instances[index];
            if (!instance.source_entity) continue;
            auto& entity = entities.at(*instance.source_entity);
            Batch batch;
            batch.instance = index;
            batch.original_texture = instance.texture_index;
            auto& null_material = batch.null_material;
            null_material.source_profile = context.world.source_profile;
            null_material.source_shader_model2 = instance.material_state.source_shader_model2;
            null_material.source_cube_capable = instance.material_state.source_cube_capable;
            null_material.entity_skins = instance.material_state.entity_skins;
            if (instance.source_group) {
                const auto& group = source_model_group(scene, entity.id, *instance.source_group);
                if (instance.source_skin) {
                    const auto& skin = group.skins.at(*instance.source_skin);
                    if (!skin.material.empty()) {
                        if (skin.material.size() != 17)
                            throw std::runtime_error("Incomplete original MATERIAL record in source NULL binding");
                        std::copy_n(skin.material.begin(), 4, null_material.diffuse.begin());
                        std::copy_n(skin.material.begin() + 4, 4, null_material.ambient.begin());
                        std::copy_n(skin.material.begin() + 8, 4, null_material.specular.begin());
                        std::copy_n(skin.material.begin() + 12, 4, null_material.emissive.begin());
                        null_material.power = skin.material[16];
                        null_material.color_known_mask = 0xfff;
                        null_material.power_known = null_material.model_material_present = null_material.ambient_alpha_known = true;
                    }
                    if (!skin.effect.empty()) {
                        const auto filename = std::filesystem::path(skin.effect).filename().string();
                        const auto member = filename.rfind("::");
                        null_material.technique = material_from_effect(member == std::string::npos
                            ? std::string_view(filename) : std::string_view(filename).substr(member + 2));
                    }
                }
                if (*instance.source_group == 0 && !group.triangle_materials.empty()) {
                    const auto first_skin = group.triangle_materials.front();
                    batch.first_faces = first_skin == 0xffffffffu ? !instance.source_skin
                        : instance.source_skin == std::optional<std::uint32_t>(first_skin);
                }
                if (group.frames.size() > 1 && std::find(entity.animated_groups.begin(), entity.animated_groups.end(), *instance.source_group) == entity.animated_groups.end())
                    entity.animated_groups.push_back(*instance.source_group);
                if (batch.first_faces && !group.skins.empty()) batch.selectable_skins = &skins(entity.id, *instance.source_group);
            }
            configure_material_state(null_material, {}, instance.flags,
                static_cast<float>(physics::number(context.visuals[entity.id].alpha)));
            batch.color_family = batch.first_faces ? color_family(entity.id, context.ids) : -1;
            entity.color_bound = entity.color_bound || batch.color_family >= 0
                || entity.id == context.ids.p_arac_kaporta || entity.id == context.ids.p_robot_kaporta;
            if (entity.id == context.ids.p_arac_kaporta || entity.id == context.ids.p_robot_kaporta) {
                const auto family = entity.id == context.ids.p_arac_kaporta ? 0u : 1u;
                batch.body_variants = std::make_unique<std::array<NativeMaterialState, 3>>();
                for (std::size_t variant = 0; variant < 3; ++variant) {
                    auto& state = (*batch.body_variants)[variant];
                    state = instance.material_state;
                    state.source_profile = context.world.source_profile;
                    state.technique = material_bindings[family * 3 + variant].technique;
                    state.material_skins = material_bindings[family * 3 + variant].material_skins;
                    state.material_skins_known_mask = material_bindings[family * 3 + variant].material_skins_known_mask;
                    configure_material_state(state, body_materials[family * 3 + variant], instance.flags,
                        static_cast<float>(physics::number(context.visuals[entity.id].alpha)));
                    initialize_source_material_event_state(state, body_materials[family * 3 + variant]);
                }
                instance.material.reserve(std::max({instance.material.size(), body_materials[family * 3].size(),
                    body_materials[family * 3 + 1].size(), body_materials[family * 3 + 2].size()}));
            }
            entity.batches.push_back(std::move(batch));
        }
        for (auto& entity : entities)
            if (!entity.animated_groups.empty()) prepare_entity_animation(scene, entity.id);

        for (const auto& entity : entities) {
            for (const auto& batch : entity.batches) {
                const auto& instance = scene.instances[batch.instance];
                cache_named_material(instance.material, instance.material_state);
                if (batch.body_variants) {
                    const auto family = entity.id == context.ids.p_arac_kaporta ? 0u : 1u;
                    for (std::size_t variant = 0; variant < 3; ++variant)
                        cache_named_material(body_materials[family * 3 + variant], (*batch.body_variants)[variant]);
                }
            }
        }
        std::vector<std::string_view> initial_materials;
        for (const auto& source : context.world.entities) {
            const auto name = source.source_initial_material_identity;
            if (!source.source_initial_material_known || name.empty() || named_materials.contains(name)
                    || std::find(initial_materials.begin(), initial_materials.end(), name) != initial_materials.end()) continue;
            initial_materials.push_back(name);
        }
        // These are actual source facing/tree assignments after their wait(1),
        // regardless of which MATERIAL was selected for cold GPU preparation.
        for (const std::string_view name : {"Mtl_agac_don", "Mtl_agaclar"})
            if (!named_materials.contains(name)
                    && std::find(initial_materials.begin(), initial_materials.end(), name) == initial_materials.end())
                initial_materials.push_back(name);
        if (!initial_materials.empty()) {
            const auto definitions = assets::resolve_material_resources(owner, initial_materials, context.world.source_profile);
            for (std::size_t index = 0; index < definitions.size(); ++index) {
                const auto& definition = definitions[index];
                if (!definition.definition_known)
                    throw std::logic_error("Source initial MATERIAL definition is unavailable");
                NativeMaterialState binding;
                binding.source_profile = context.world.source_profile;
                binding.material_skins_known_mask = definition.material_skins_known_mask;
                if (!definition.effect.empty())
                    binding.technique = material_from_effect(definition.effect.filename().string());
                for (std::size_t slot = 0; slot < definition.skins.size(); ++slot)
                    if (!definition.skins[slot].empty())
                        binding.material_skins[slot] = cache_texture(scene, definition.skins[slot],
                            (definition.material_skin_cube_mask & (1u << slot)) != 0);
                configure_material_state(binding, initial_materials[index], 0);
                cache_named_material(initial_materials[index], binding);
            }
        }
        std::size_t material_name_capacity = 0;
        // The imported SHM route clears its declared shared MATERIAL tangent bit.
        // Apply to the named cache too, so later assignments inherit the source write.
        for (const auto& source : context.world.entities) {
            if (source.model_index == assets::no_world_model
                    || context.world.models.at(source.model_index).imported_mesh.indices.empty()) continue;
            const auto name = source_imported_mesh_material(source.action);
            if (const auto found = named_materials.find(name); !name.empty() && found != named_materials.end())
                apply_source_imported_mesh_tangent_off(found->second.state, source.action);
        }
        for (const auto& [name, material] : named_materials)
            material_name_capacity = std::max(material_name_capacity, name.size());
        for (auto& instance : scene.instances) instance.material.reserve(material_name_capacity);
        world_entities = context.world.entities.data(); world_models = context.world.models.data();
        source_profile = context.world.source_profile;
        scene_instances = scene.instances.data(); scene_meshes = scene.meshes.data(); visual_states = context.visuals.data();
        entity_count = context.world.entities.size(); model_count = context.world.models.size();
        instance_count = scene.instances.size(); mesh_count = scene.meshes.size(); texture_count = scene.textures.size();
        synchronize();
        scene.counts = submitted_scene_counts(FrameSnapshot{scene, scene.instances, {}});
    }
    void reconcile_sprite_caches() {
        // 1003bc10 ordinary predraw invokes mode zero only for +60 != 0.
        // Its raw-angle change test reseeds all three cached FLOAT fields;
        // an unchanged authored triple retains the previous draw's mutation.
        for (const auto& binding : scene.sprite_bindings) {
            const auto id = binding.source_entity;
            auto& cache = sprite_caches[id];
            if (!cache.qualified || context.world.entities[id].native_prepared_inactive) continue;
            const auto authored = physics::body_source(context.physics.body(id).angles);
            const std::array<std::int32_t, 3> words{authored.x.raw, authored.y.raw, authored.z.raw};
            if (!cache.born || (cache.dynamic && words != cache.authored_words)) {
                cache.cached_angles = {float(physics::number(authored.x)),
                    float(physics::number(authored.y)), float(physics::number(authored.z))};
                cache.authored_words = words;
                cache.born = true;
            }
        }
    }


    void synchronize() {
        if (!scene.world || &*scene.world != &context.world || world_entities != context.world.entities.data()
                || world_models != context.world.models.data() || scene_instances != scene.instances.data()
                || scene_meshes != scene.meshes.data() || visual_states != context.visuals.data()
                || context.world.entities.size() != entity_count || context.world.models.size() != model_count
                || context.world.source_profile != source_profile
                || context.physics.size() != entity_count || context.visuals.size() != entity_count
                || scene.instances.size() != instance_count || scene.meshes.size() != mesh_count || scene.textures.size() != texture_count)
            throw std::logic_error("Source game render/physics identity changed after GPU preparation");
        if (source_history_epoch != context.state.history_epoch) {
            // The source producers record actual reset/mode/VIEW initialization
            // events. Pose synchronization itself never invalidates history.
            scene.history_epoch += context.state.history_epoch - source_history_epoch;
            source_history_epoch = context.state.history_epoch;
        }
        const auto raw_color = context.state.c4_texture_rengi.raw;
        if (raw_color < 0 || raw_color >= 10 * 1024 || raw_color % 1024 != 0)
            throw std::runtime_error("Source game color is not a valid original texture-array index");
        const int color = raw_color / 1024;
        const auto variant = color == 2 ? 1u : color == 4 || color == 8 ? 2u : 0u;
        for (auto& entity : entities) {
            if (entity.batches.empty()) continue;
            const auto& body = context.physics.body(entity.id);
            const auto& visual = context.visuals[entity.id];
            const auto& source = context.world.entities[entity.id];
            const float ambient = source.source_ambient.value_or(source.ambient);
            const float albedo = source.source_albedo.value_or(source.albedo);
            if (!entity.sprite && (!entity.initialized || entity.inactive != source.native_prepared_inactive)) {
                for (const auto& batch : entity.batches)
                    scene.instances[batch.instance].native_prepared_inactive = source.native_prepared_inactive;
                entity.inactive = source.native_prepared_inactive;
            }
            auto flags = (source.flags | source.source_flags_set) & ~source.source_flags_clear;
            // Collision flags have separate recovered bits; never let them
            // replace the authored render flags or the source visual reducer.
            const auto body_flag = [&](std::uint32_t bit, bool enabled) {
                flags = enabled ? flags | bit : flags & ~bit;
            };
            body_flag(0x200u, body.flags.passable);
            body_flag(0x04000000u, body.flags.polygon);
            body_flag(2u, body.flags.flag2);
            body_flag(8u, body.flags.flag4);
            flags = visual.invisible ? flags | invisible : flags & ~invisible;
            flags = visual.transparent ? flags | transparent : flags & ~transparent;
            if (!entity.initialized || !same(entity.position, body.position) || !same(entity.angles, body.angles) || !same(entity.scale, body.scale)) {
                if (!entity.sprite) {
                    const auto matrix = body_transform(body);
                    for (const auto& batch : entity.batches) scene.instances[batch.instance].model_to_world = matrix;
                }
                entity.position = body.position; entity.angles = body.angles; entity.scale = body.scale;
            }
            if (!entity.initialized || entity.revision != visual.revision) {
                if (!entity.initialized || entity.sample.first_frame != visual.sample.first_frame
                        || entity.sample.second_frame != visual.sample.second_frame || entity.sample.blend != visual.sample.blend)
                    for (const auto group : entity.animated_groups) sample_entity_pose(scene, entity.id, group, visual.sample);
                entity.revision = visual.revision;
                entity.sample = visual.sample;
            }
            const bool material_changed = visual.material_identity_known
                && (!entity.rendered_material_known || entity.rendered_material != visual.material_identity);
            if (!entity.initialized || entity.flags != flags || entity.alpha.raw != visual.alpha.raw
                    || entity.skin != visual.skin || entity.ambient != ambient || entity.albedo != albedo
                    || material_changed || (entity.color_bound && entity.color != color)) {
                for (const auto& batch : entity.batches) {
                    auto& instance = scene.instances[batch.instance];
                    // Keep the recovered private world-sprite overlay branch
                    // when an alpha/skin-only update leaves its pose cache valid.
                    instance.flags = flags | (instance.source_sprite && instance.source_sprite_state_known
                        && !instance.source_sprite_no_depth_write ? 0x1000u : 0u);
                    std::string_view material = instance.material;
                    if (material_changed) {
                        const auto entity_skins = instance.material_state.entity_skins;
                        material = visual.material_identity;
                        if (material.empty()) {
                            instance.material_state = batch.null_material;
                        } else {
                            const auto& selected = named_material(material);
                            instance.material_state = selected.builtin && batch.null_material.model_material_present
                                ? batch.null_material : selected.state;
                            if (selected.builtin) {
                                instance.material_state.technique = batch.null_material.technique;
                                copy_material_event_state(instance.material_state, selected.state);
                            }
                        }
                        instance.material_state.entity_skins = entity_skins;
                        instance.material_state.model_material_present = batch.null_material.model_material_present;
                        instance.material.assign(material);
                    } else if (!visual.material_identity_known && batch.body_variants
                            && (!entity.initialized || entity.color != color)) {
                        const auto family = entity.id == context.ids.p_arac_kaporta ? 0u : 1u;
                        material = body_materials[family * 3 + variant];
                        copy_material_event_state((*batch.body_variants)[variant], named_material(material).state);
                        instance.material_state = (*batch.body_variants)[variant];
                        instance.material.assign(material);
                    }
                    instance.material_state.entity_ambient_percent = ambient;
                    instance.material_state.entity_albedo_percent = albedo;
                    if (batch.selectable_skins) {
                        const auto selected_skin = std::min<std::size_t>(
                            visual.skin > 1 ? static_cast<std::size_t>(visual.skin - 1) : 0,
                            batch.selectable_skins->size());
                        instance.texture_index = selected_skin == 0 ? batch.original_texture
                            : std::optional<std::uint32_t>((*batch.selectable_skins)[selected_skin - 1]);
                        instance.material_state.entity_skins[0] = instance.texture_index;
                    }
                    if (batch.color_family >= 0 && visual.skin == 1) {
                        instance.texture_index = colors[static_cast<std::size_t>(batch.color_family) * 10 + color];
                        instance.material_state.entity_skins[0] = instance.texture_index;
                    }
                    finish_material(scene, instance, material, static_cast<float>(physics::number(visual.alpha)));
                    if (const auto shared = named_materials.find(material); shared != named_materials.end())
                        copy_material_event_state(instance.material_state, shared->second.state);
                    ensure_source_tangents(scene, instance);
                }
                entity.flags = flags; entity.alpha = visual.alpha; entity.skin = visual.skin; entity.color = color;
                entity.ambient = ambient; entity.albedo = albedo;
                entity.rendered_material = visual.material_identity;
                entity.rendered_material_known = visual.material_identity_known;
            }
            entity.initialized = true;
        }
    }


    void advance_material_processes(physics::Var ticks, physics::Var frame_ticks) {
        if (ticks.raw < 0) throw std::invalid_argument("Source material time step is negative");
        consume_material_source_writes();
        for (auto& [name, material] : named_materials) {
            bool resumed = false;
            for (const auto id : material.water_receivers) {
                const auto& receiver = material.water_processes[id];
                for (std::uint64_t process = 0; process < receiver.waiting_count; ++process) {
                    if (!game::source_wait1_context_ready(context.state.freeze_mode, frame_ticks,
                            receiver.saved_my == game::no_entity)) continue;
                    resume_water_material_event(material.water_event, material.state, ticks);
                    resumed = true;
                }
            }
            if (resumed) publish_material(name, material);
        }
    }

    void reconcile_material_events(physics::Var ticks) {
        if (ticks.raw < 0) throw std::invalid_argument("Source material time step is negative");
        reconcile_sprite_caches();
        consume_material_source_writes();
        for (auto& entity : entities) {
            const auto& visual = context.visuals[entity.id];
            const auto& source = context.world.entities[entity.id];
            if (entity.material_prior_pending && !entity.material_snapshot_known && source.source_previous_material_known) {
                entity.material_snapshot = source.source_previous_material_identity;
                entity.material_snapshot_known = true;
            }
            if (!visual.material_identity_known) {
                if (!entity.material_prior_pending) entity.material_snapshot_known = false;
                continue;
            }
            if (visual.material_identity.empty() && source.model_index != assets::no_world_model) {
                const auto flags = (source.flags | source.source_flags_set) & ~source.source_flags_clear;
                const auto selector = ((flags & 0x20000u) ? 1u : 0u) | ((flags & 0x400000u) ? 2u : 0u);
                const auto fallback = null_materials.at(source.model_index)[selector];
                if (fallback.known) context.set_material(entity.id, fallback.identity);
            }
            // Preserve the proven constructor snapshot until actual activation.
            // Unqualified creation paths still do not invent a NULL prior.
            const bool changed = entity.material_snapshot_known
                && entity.material_snapshot != visual.material_identity;
            entity.material_snapshot = visual.material_identity;
            entity.material_snapshot_known = true;
            entity.material_prior_pending = false;
            if (!changed || visual.material_identity.empty()) continue;
            auto& material = named_material(visual.material_identity);
            auto& registered = material.state;
            if (!source_material_default_event_eligible(registered)) continue;
            switch (registered.source_event) {
            case SourceMaterialEvent::WaterLoop:
                start_water_material_event(material.water_event, registered, ticks);
                if (material.water_processes[entity.id].waiting_count++ == 0)
                    material.water_receivers.push_back(entity.id);
                break;
            case SourceMaterialEvent::EnvironmentInitialize:
            case SourceMaterialEvent::EnvironmentInitializeForGatedView:
                registered.source_event = registered.source_event == SourceMaterialEvent::EnvironmentInitialize
                    ? SourceMaterialEvent::EnvironmentView : SourceMaterialEvent::EnvironmentViewNonzeroSkill1;
                registered.source_flags |= 0x40000u;
                registered.source_flags_known_mask |= 0x40000u;
                break;
            case SourceMaterialEvent::EnvironmentCubeConversion:
                // The actual cube upload is already prepared. This callback
                // retains EVENT and never enables the separate VIEW callback.
                break;
            default:
                break;
            }
            publish_material(visual.material_identity, material);
        }
    }

    void synchronize_view(const NativeGameVisualState& visuals, const SourceSpriteView& sprite_view) {
        for (auto& binding : scene.sprite_bindings) {
            const auto& body = context.physics.body(binding.source_entity);
            const auto floats = [](physics::Vec3 value) {
                return std::array<float, 3>{float(value.x), float(value.y), float(value.z)};
            };
            SourceSpritePose pose;
            pose.origin = floats(body.position);
            pose.authored_angles = floats(physics::native(physics::body_source(body.angles)));
            auto& cache = sprite_caches[binding.source_entity];
            pose.cached_angles = cache.cached_angles;
            pose.cached_angles_known = cache.qualified && cache.born;
            pose.scale = floats(body.scale);
            pose.authored_flags = entities.at(binding.source_entity).flags;
            pose.internal_flags_known_mask = cache.qualified ? 0x520000u : 0u;
            synchronize_source_sprite(scene, binding.source_entity, pose, sprite_view);
            if (binding.cached_angles_after_draw_known)
                cache.cached_angles = binding.cached_angles_after_draw;
        }
        for (auto& entity : entities) {
            const auto skin = context.visuals[entity.id].skin;
            if (entity.palette_skin != skin) {
                // This accepted root VIEW has no VIEW MATERIAL context (10047410).
                // 10061070 maps negative triangle IDs to subset 0; 1004db10 uploads
                // those IDs unchanged. Only subset 0 retains ENTITY.skin (10050570).
                for (const auto& batch : entity.batches) {
                    auto& instance = scene.instances[batch.instance];
                    if (!instance.source_group) continue;
                    instance.source_skin_ambient_alpha.reset();
                    instance.source_skin_override_known = false;
                    const auto& group = source_model_group(scene, entity.id, *instance.source_group);
                    if (group.skins.empty()) {
                        // 1004eb30 selects index -1 and calls 10047410(NULL), clearing the override.
                        instance.source_skin_override_known = true;
                        continue;
                    }
                    const auto subset = instance.source_skin.value_or(0);
                    const auto selected = subset == 0 ? std::min<std::size_t>(
                        skin > 1 ? static_cast<std::size_t>(skin - 1) : 0, group.skins.size() - 1) : subset;
                    if (selected >= group.skins.size()) continue;
                    const auto& palette = group.skins[selected];
                    if ((palette.flags & 0x10) == 0) {
                        instance.source_skin_override_known = true;
                    } else if (palette.material.size() == 17 && std::isfinite(palette.material[7])) {
                        instance.source_skin_ambient_alpha = palette.material[7];
                        instance.source_skin_override_known = true;
                    }
                }
                entity.palette_skin = skin;
            }
            if (!entity.material_snapshot_known || entity.material_snapshot.empty() || entity.batches.empty()) continue;
            auto& shared = named_material(entity.material_snapshot);
            auto& registered = shared.state;
            if (!registered.source_event_known
                    || (registered.source_flags_known_mask & 0x40000u) == 0
                    || (registered.source_flags & 0x40000u) == 0) continue;
            if (registered.source_event == SourceMaterialEvent::EnvironmentViewNonzeroSkill1) {
                std::uint32_t raw_skill1;
                if (registered.texture_factor_known) raw_skill1 = registered.texture_factor; // pixel_for_vec() DWORD.
                else if ((registered.skills_known_mask & 1u) != 0)
                    raw_skill1 = std::bit_cast<std::uint32_t>(registered.skills[0]); // Source float() IEEE word.
                else throw std::runtime_error("Source MATERIAL conditional view event requires its actual raw skill1");
                if (raw_skill1 == 0) continue;
            } else if (registered.source_event != SourceMaterialEvent::EnvironmentView) continue;
            if (!visuals.view_inputs_known)
                throw std::runtime_error("Source MATERIAL view event requires actual source inverse-view inputs");
            if (!view_initialized || last_view != visuals.world_to_view
                    || last_view_position != visuals.view_position) {
                inverse_view = source_inverse_view_matrix(visuals);
                last_view = visuals.world_to_view;
                last_view_position = visuals.view_position;
                view_initialized = true;
            }
            update_environment_material_view(registered, inverse_view);
            for (const auto& batch : entity.batches) {
                auto& instance = scene.instances[batch.instance];
                if (instance.material == entity.material_snapshot)
                    copy_material_event_state(instance.material_state, registered);
            }
        }
    }
};

GameRenderBridge::GameRenderBridge(RenderScene& scene, game::GameContext& context)
    : impl_(std::make_unique<Impl>(scene, context)) {}
GameRenderBridge::~GameRenderBridge() = default;
void GameRenderBridge::advance_material_processes(physics::Var ticks, physics::Var frame_ticks) {
    impl_->advance_material_processes(ticks, frame_ticks);
}
void GameRenderBridge::reconcile_material_events(physics::Var ticks) { impl_->reconcile_material_events(ticks); }
void GameRenderBridge::synchronize() { impl_->synchronize(); }
void GameRenderBridge::synchronize_view(const NativeGameVisualState& visuals, const SourceSpriteView& sprite_view) {
    impl_->synchronize_view(visuals, sprite_view);
}
} // namespace c4::rendering
