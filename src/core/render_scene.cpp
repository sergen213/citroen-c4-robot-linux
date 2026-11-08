#include "render_scene.hpp"

#include "game/ui/NativeUi.hpp"
#include "game/ui/SourceViewSprite.hpp"
#include "rendering/MaterialEvents.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace c4::rendering {
namespace {
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr std::array<float, 3> camera_right{0.70710678f, 0.70710678f, 0};
constexpr std::array<float, 3> camera_up{-0.40824829f, 0.40824829f, 0.81649658f};
constexpr std::array<float, 3> camera_back{0.57735027f, -0.57735027f, 0.57735027f};
constexpr std::uint32_t invisible = 1u << 8;
constexpr std::uint32_t no_material = std::numeric_limits<std::uint32_t>::max();

MaterialTechnique technique_from_path(const std::filesystem::path& effect) {
    const auto filename = effect.filename().string();
    const auto member = filename.rfind("::");
    return material_from_effect(member == std::string::npos
        ? std::string_view(filename) : std::string_view(filename).substr(member + 2));
}

bool has_clip(const assets::ModelGroup& group, std::string_view clip) {
    const auto length = std::min<std::size_t>(clip.size(), 16);
    return std::any_of(group.frames.begin(), group.frames.end(), [&](const auto& frame) {
        return frame.name.size() >= length && std::equal(clip.begin(), clip.begin() + length,
            frame.name.begin(), [](unsigned char a, unsigned char b) {
                return std::tolower(a) == std::tolower(b);
            });
    });
}

std::span<const std::uint8_t> skin_palette(RenderScene& scene) {
    if (!scene.world || !scene.world->has_palette) return {};
    if (!scene.skin_palette_ready) {
        for (std::size_t i = 0; i < scene.skin_palette_rgb.size(); i += 3) {
            scene.skin_palette_rgb[i] = scene.world->palette[i + 2];
            scene.skin_palette_rgb[i + 1] = scene.world->palette[i + 1];
            scene.skin_palette_rgb[i + 2] = scene.world->palette[i];
        }
        scene.skin_palette_ready = true;
    }
    return scene.skin_palette_rgb;
}

std::uint32_t reference_texture(RenderScene& scene, std::uint32_t index) {
    if (std::find(scene.referenced_texture_indices.begin(), scene.referenced_texture_indices.end(), index)
            == scene.referenced_texture_indices.end())
        scene.referenced_texture_indices.push_back(index);
    return index;
}

std::uint32_t add_texture(RenderScene& scene, assets::GpuImage gpu, std::string source) {
    if (scene.textures.size() >= std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Scene texture index capacity exhausted");
    assets::Image metadata;
    metadata.width = gpu.runtime ? gpu.runtime->source_width : gpu.width;
    metadata.height = gpu.runtime ? gpu.runtime->source_height : gpu.height;
    metadata.face_count = gpu.face_count;
    metadata.source_format = gpu.source_format;
    metadata.source_bits_per_pixel = gpu.source_bits_per_pixel;
    metadata.source_alpha_known = gpu.source_alpha_known;
    metadata.source_alpha_flag = gpu.source_alpha_flag;
    const auto index = static_cast<std::uint32_t>(scene.textures.size());
    scene.textures.push_back(std::move(metadata));
    scene.gpu_textures.push_back(std::move(gpu));
    scene.texture_sources.push_back(std::move(source));
    return index;
}

std::uint32_t cache_source_texture(RenderScene& scene, const std::filesystem::path& path,
    bool cube, const assets::GpuImageUploadOptions& options, bool referenced = true) {
    const auto source = path.string();
    for (std::size_t i = 0; i < scene.texture_sources.size(); ++i) {
        if (scene.texture_sources[i] != source) continue;
        const auto faces = scene.gpu_textures[i].face_count;
        if (faces == (cube ? 6u : 1u) || (!cube && !referenced))
            return referenced ? reference_texture(scene, static_cast<std::uint32_t>(i))
                : static_cast<std::uint32_t>(i);
        if (!cube && faces == 6 && scene.gpu_textures[i].source_format == assets::ImageSourceFormat::Dds)
            throw std::runtime_error("Original DDS cubemap cannot be bound as a 2D bitmap: " + source);
    }
    assets::GpuImage gpu;
    if (cube) {
        const auto strip = cache_source_texture(scene, path, false, options, false);
        if (scene.gpu_textures.at(strip).face_count == 6)
            return referenced ? reference_texture(scene, strip) : strip;
        // Both representations may be used later; retain the original first-upload strip.
        gpu = assets::gpu_cubemap_from_image(assets::GpuImage(scene.gpu_textures.at(strip)));
    } else gpu = assets::load_gpu_image(path, options);
    if (!cube && referenced && gpu.face_count != 1)
        throw std::runtime_error("Original cube bitmap requires an environment binding: " + source);
    const auto index = add_texture(scene, std::move(gpu), source);
    return referenced ? reference_texture(scene, index) : index;
}

void generate_binding_tangents(RenderScene& scene, TangentBinding& binding) {
    for (const auto index : binding.mesh_indices)
        if (scene.meshes.at(index).pose_revision == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Scene pose revision exhausted");
    assets::generate_source_tangents(binding.vertices, binding.original_indices, binding.channel);
    binding.generated = true;
    for (const auto index : binding.mesh_indices) {
        auto& mesh = scene.meshes.at(index);
        mesh.tertiary_uv = binding.channel;
        mesh.tangent_provenance = assets::TangentProvenance::SourceAlgorithmNativePort;
        ++mesh.pose_revision;
    }
}

void update_pose_binding(RenderScene& scene, PoseBinding& binding, assets::AnimationSample sample) {
    if (binding.sample_known && sample.first_frame == binding.sample.first_frame
            && sample.second_frame == binding.sample.second_frame && sample.blend == binding.sample.blend)
        return;
    for (const auto index : binding.mesh_indices)
        if (scene.meshes[index].pose_revision == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Scene pose revision exhausted");
    const bool changed = assets::sample_pose(*binding.group, sample, scene.posed_vertices[binding.vertices_index]);
    binding.sample = sample;
    binding.sample_known = true;
    if (changed) {
        if (binding.tangent_binding) {
            auto& tangents = scene.tangent_bindings.at(*binding.tangent_binding);
            if (tangents.request_count) {
                generate_binding_tangents(scene, tangents);
                return;
            }
            tangents.generated = false; // A later request must derive the current pose, not the old channel.
        }
        for (const auto index : binding.mesh_indices) ++scene.meshes[index].pose_revision;
    }
}

void include_bounds(RenderScene& scene, const Instance& instance, bool count_geometry = true) {
    if (!instance_visible(instance)) return;
    const auto& mesh = scene.meshes.at(instance.mesh_index);
    const auto& m = instance.model_to_world;
    if (count_geometry) {
        scene.counts.mesh_vertices += mesh.vertices.size();
        scene.counts.mesh_triangles += mesh.indices.size() / 3;
        if (instance.kind == InstanceKind::World) {
            scene.counts.world_vertices += mesh.vertices.size();
            scene.counts.world_triangles += mesh.indices.size() / 3;
        }
    }
    for (const auto index : mesh.indices) {
        if (index >= mesh.vertices.size()) throw std::runtime_error("Scene mesh index exceeds vertex count");
        const auto& v = mesh.vertices[index];
        const std::array<float, 3> point{m[0]*v.x + m[4]*v.y + m[8]*v.z + m[12],
            m[1]*v.x + m[5]*v.y + m[9]*v.z + m[13], m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(point[axis])) throw std::runtime_error("Nonfinite transformed scene geometry");
            if (!scene.has_bounds) scene.bounds_min[axis] = scene.bounds_max[axis] = point[axis];
            else {
                scene.bounds_min[axis] = std::min(scene.bounds_min[axis], point[axis]);
                scene.bounds_max[axis] = std::max(scene.bounds_max[axis], point[axis]);
            }
        }
        scene.has_bounds = true;
    }
}

struct Loader {
    RenderScene scene;
    const Settings& settings;
    explicit Loader(const Settings& value) : settings(value) { scene.source_profile = effective_source_profile(value); }
    Loader(const Settings& value, assets::WorldScene world) : settings(value) {
        scene.source_profile = world.source_profile;
        scene.world.emplace(std::move(world));
    }
    Loader(const Settings& value, const assets::GpuImageRuntimeContext& context) : Loader(value) {
        set_gpu_context(context);
    }
    Loader(const Settings& value, assets::WorldScene world, const assets::GpuImageRuntimeContext& context)
        : Loader(value, std::move(world)) { set_gpu_context(context); }
    void set_gpu_context(const assets::GpuImageRuntimeContext& context) {
        if (!context.maximum_dimension) throw std::runtime_error("Source GPU upload requires the actual texture limit");
        scene.gpu_runtime_context = context;
        upload_options.runtime = context;
    }
    bool matched_animation{};
    std::map<const assets::ModelGroup*, std::vector<std::pair<std::uint32_t, std::uint32_t>>> group_geometry;
    // Loader-lifetime keys keep every replaced shared original alive while memoized.
    std::map<std::shared_ptr<const assets::Model>, std::shared_ptr<const assets::Model>> uv_domains;
    assets::GpuImageUploadOptions upload_options;

    std::uint32_t texture(const std::filesystem::path& path) {
        return cache_source_texture(scene, path, false, upload_options);
    }

    std::optional<std::uint32_t> skin_texture(const assets::Skin& skin,
        const std::filesystem::path& external, const std::string& source) {
        if (!external.empty()) return texture(external);
        for (std::size_t i = 0; i < scene.texture_sources.size(); ++i)
            if (scene.texture_sources[i] == source)
                return reference_texture(scene, static_cast<std::uint32_t>(i));
        const auto palette = skin.format == 0 ? skin_palette(scene) : std::span<const std::uint8_t>{};
        return reference_texture(scene, add_texture(scene,
            assets::gpu_image_from_skin(skin, upload_options, palette), source));
    }

    // Ordinary MDL loader 10061070: 100613be clears the skin size, 10060e40
    // (10060f81-10060f98) records the first created skin bitmap's +0c/+10, then
    // 1006180d-1006196c multiply every stored UV by size / 1002db10(size): the
    // at-least-8 power-of-two domain this skin's upload pads into. Null: unchanged.
    // ponytail: skinless groups (original size0 -> UV*0) stay unscaled; no consumer proof yet.
    std::shared_ptr<const assets::Model> source_uv_domain(const assets::Model& model,
        const std::vector<std::vector<std::filesystem::path>>& resources, const std::string& source) {
        std::shared_ptr<assets::Model> scaled;
        for (std::size_t g = 0; g < model.groups.size(); ++g) {
            const auto& group = model.groups[g];
            if (group.skins.empty()) continue;
            const auto& runtime = scene.gpu_textures.at(*skin_texture(group.skins.front(), resources.at(g).at(0),
                source + "#group=" + std::to_string(g) + ":" + group.name + "#skin=0")).runtime;
            if (!runtime) continue; // Storage without runtime padding keeps the authored domain.
            const double u = static_cast<double>(runtime->source_width) / runtime->nominal_width;
            const double v = static_cast<double>(runtime->source_height) / runtime->nominal_height;
            if (u == 1 && v == 1) continue;
            if (!scaled) scaled = std::make_shared<assets::Model>(model);
            auto& mesh = scaled->groups[g].mesh;
            // Exact double ratio times float UV, stored as float (fmul QWORD; fstp DWORD).
            for (auto& vertex : mesh.vertices) {
                vertex.u = static_cast<float>(vertex.u * u);
                vertex.v = static_cast<float>(vertex.v * v);
            }
            for (auto& uv : mesh.secondary_uv) { // 1004f280 TEXCOORD1 copies the same stored UV.
                uv[0] = static_cast<float>(uv[0] * u);
                uv[1] = static_cast<float>(uv[1] * v);
            }
        }
        return scaled;
    }

    void entity_skin_slots(Instance& instance, const assets::ModelGroup& group,
        const std::vector<std::filesystem::path>& resources, const std::string& source) {
        instance.material_state.entity_skins[0] = instance.texture_index;
        for (std::size_t slot = 1; slot < std::min<std::size_t>(group.skins.size(), 4); ++slot)
            instance.material_state.entity_skins[slot] = skin_texture(group.skins[slot],
                resources.at(slot), source + "#skin=" + std::to_string(slot));
    }

    void finish_material(Instance& instance, std::string_view assigned_material, float alpha_percent) {
        configure_instance_material(scene, instance, assigned_material, alpha_percent);
    }

    Instance batch(const assets::Mesh& mesh, const std::string& source, const std::string& material) {
        if (mesh.indices.empty() || mesh.indices.size() % 3 != 0)
            throw std::runtime_error("Scene geometry is not a nonempty triangle mesh: " + source);
        Instance instance;
        instance.mesh_index = static_cast<std::uint32_t>(scene.meshes.size());
        instance.model_to_world = identity;
        instance.source = source;
        instance.material = material;
        const auto tangent_index = scene.tangent_bindings.size();
        scene.tangent_bindings.push_back({mesh.vertices, mesh.indices, {instance.mesh_index}});
        scene.meshes.push_back({mesh.vertices, mesh.indices, 0, mesh.secondary_uv, mesh.tertiary_uv,
            mesh.tangent_provenance, tangent_index});
        ++scene.counts.materials;
        return instance;
    }

    std::vector<Instance> model_batches(const assets::Model& model,
        const std::vector<std::vector<std::filesystem::path>>& resources,
        const std::string& source, const std::string& material,
        const std::filesystem::path& override_texture = {}) {
        std::vector<Instance> result;
        for (std::size_t g = 0; g < model.groups.size(); ++g) {
            const auto& group = model.groups[g];
            if (group.mesh.indices.empty()) continue;
            const bool source_skin_override_known = std::none_of(group.skins.begin(), group.skins.end(),
                [](const auto& skin) { return (skin.flags & 0x10) != 0; });
            auto [cached, inserted] = group_geometry.try_emplace(&group);
            if (inserted) {
                ++scene.counts.groups;
                std::span<const assets::Vertex> vertices = group.mesh.vertices;
                std::optional<std::size_t> pose_binding;
                if (!settings.animation.empty() && has_clip(group, settings.animation)) {
                    const auto pose = assets::animation_sample(group, settings.animation, settings.percentage, false);
                    scene.posed_vertices.emplace_back(group.mesh.vertices.size());
                    assets::sample_pose(group, pose, scene.posed_vertices.back());
                    vertices = scene.posed_vertices.back();
                    pose_binding = scene.pose_bindings.size();
                    scene.pose_bindings.push_back({&group, scene.posed_vertices.size() - 1, {}, pose,
                        std::nullopt, static_cast<std::uint32_t>(g), true});
                    matched_animation = true;
                }
                const auto tangent_index = scene.tangent_bindings.size();
                scene.tangent_bindings.push_back({vertices, group.mesh.indices});
                if (pose_binding) scene.pose_bindings[*pose_binding].tangent_binding = tangent_index;
                const auto triangles = group.mesh.indices.size() / 3;
                if (group.mesh.indices.size() % 3 != 0 || group.triangle_materials.size() != triangles)
                    throw std::runtime_error("Model triangle material count does not match geometry: " + source);
                std::map<std::uint32_t, std::vector<std::uint32_t>> partitions;
                const auto uniform = std::all_of(group.triangle_materials.begin(), group.triangle_materials.end(),
                    [&](auto id) { return id == group.triangle_materials.front(); });
                if (uniform) partitions.emplace(group.triangle_materials.front(), std::vector<std::uint32_t>{});
                else for (std::size_t t = 0; t < triangles; ++t) {
                    auto& indices = partitions[group.triangle_materials[t]];
                    indices.insert(indices.end(), group.mesh.indices.begin() + t*3, group.mesh.indices.begin() + t*3 + 3);
                }
                for (auto& [skin_id, indices] : partitions) {
                    std::span<const std::uint32_t> view = group.mesh.indices;
                    if (!uniform) {
                        scene.material_indices.push_back(std::move(indices));
                        view = scene.material_indices.back();
                    }
                    const auto mesh_index = static_cast<std::uint32_t>(scene.meshes.size());
                    scene.meshes.push_back({vertices, view, 0, group.mesh.secondary_uv, group.mesh.tertiary_uv,
                        group.mesh.tangent_provenance, tangent_index});
                    scene.tangent_bindings[tangent_index].mesh_indices.push_back(mesh_index);
                    cached->second.emplace_back(skin_id, mesh_index);
                    if (pose_binding) scene.pose_bindings[*pose_binding].mesh_indices.push_back(mesh_index);
                }
            }
            for (const auto& [skin_id, mesh_index] : cached->second) {
                const auto label = source + "#group=" + std::to_string(g) + ":" + group.name;
                Instance instance;
                // Prior D3DMATERIAL is selected by ENTITY.skin, not this triangle's bitmap batch.
                // Without that captured selector, any possible palette override remains unknown.
                instance.source_skin_override_known = source_skin_override_known;
                instance.mesh_index = mesh_index;
                instance.source_group = static_cast<std::uint32_t>(g);
                if (skin_id != no_material) instance.source_skin = skin_id;
                instance.model_to_world = identity;
                instance.source = label;
                instance.material = material;
                ++scene.counts.materials;
                if (!settings.texture.empty()) instance.texture_index = texture(settings.texture);
                else if (!override_texture.empty()) instance.texture_index = texture(override_texture);
                if (skin_id != no_material) {
                    if (skin_id >= group.skins.size())
                        throw std::runtime_error("Model triangle references an absent skin: " + label);
                    const auto& skin = group.skins[skin_id];
                    if (!skin.material.empty()) {
                        if (skin.material.size() != 17) throw std::runtime_error("Incomplete original material record: " + label);
                        std::copy_n(skin.material.begin(), 4, instance.diffuse.begin());
                        std::copy_n(skin.material.begin(), 4, instance.material_state.diffuse.begin());
                        std::copy_n(skin.material.begin() + 4, 4, instance.material_state.ambient.begin());
                        std::copy_n(skin.material.begin() + 8, 4, instance.material_state.specular.begin());
                        std::copy_n(skin.material.begin() + 12, 4, instance.material_state.emissive.begin());
                        instance.material_state.power = skin.material[16];
                        instance.material_state.color_known_mask = 0xfff;
                        instance.material_state.power_known = true;
                        instance.material_state.model_material_present = true;
                        instance.material_state.ambient_alpha_known = material.empty();
                    }
                    if (instance.material.empty()) instance.material = skin.effect.empty() ? skin.name : skin.effect;
                    if (!skin.effect.empty()) instance.material_state.technique = technique_from_path(skin.effect);
                    if (!instance.texture_index) {
                        instance.texture_index = skin_texture(skin, resources.at(g).at(skin_id),
                            label + "#skin=" + std::to_string(skin_id));
                    }
                }
                entity_skin_slots(instance, group, resources.at(g), label);
                finish_material(instance, material, 50); // Original entity alpha default.
                result.push_back(std::move(instance));
            }
        }
        return result;
    }

    void world_scene() {
        if (!scene.world) {
            assets::GpuImageUploadOptions sprite_upload{true, 16, 32, scene.gpu_runtime_context};
            if (sprite_upload.runtime) sprite_upload.runtime->bitmap_flags |= 0x40000; // Original mip_sprites=2 constructor.
            scene.world.emplace(assets::load_world(settings.world, scene.source_profile, sprite_upload));
        }
        auto& world = *scene.world;
        scene.source_profile = world.source_profile;
        scene.counts.world_models = world.models.size();
        scene.counts.world_entities = 0;
        scene.counts.world_objects = world.object_count;
        scene.counts.unresolved_actions = world.unresolved_actions.size();
        scene.counts.unresolved_materials = world.unresolved_materials.size();
        scene.entity_lighting_caches.resize(world.entities.size());
        std::vector<std::optional<std::vector<Instance>>> models(world.models.size());
        for (const auto& entity : world.entities) {
            const auto flags = (entity.flags | entity.source_flags_set) & ~entity.source_flags_clear;
            upload_options.overlay = (flags & (1u << 12)) != 0;
            const bool sprite = entity.model_index != assets::no_world_model
                && !world.models.at(entity.model_index).sprite.rgba.empty();
            if (!entity.native_prepared_inactive) {
                ++scene.counts.world_entities;
                if (sprite) ++scene.counts.sprite_entities;
            }
            if (!entity.native_prepared_inactive && (flags & invisible)) {
                ++scene.counts.hidden_entities;
                if (entity.model_index == assets::no_world_model) ++scene.hidden_unrenderable_entities;
            }
            if (entity.model_index == assets::no_world_model) continue;
            auto& model = world.models.at(entity.model_index);
            if (sprite) {
                const auto entity_index = static_cast<std::uint32_t>(&entity - world.entities.data());
                const auto vertices = ui::original_view_sprite_vertices(
                    static_cast<float>(model.sprite.width), static_cast<float>(model.sprite.height));
                std::optional<std::uint32_t> texture_index;
                for (std::size_t i = 0; i < scene.texture_sources.size(); ++i)
                    if (scene.texture_sources[i] == model.resource.string() && scene.gpu_textures[i].face_count == 1) {
                        texture_index = reference_texture(scene, static_cast<std::uint32_t>(i));
                        break;
                    }
                if (!texture_index) {
                    if (!model.sprite_gpu) throw std::runtime_error("Original sprite GPU cache was not prepared: " + model.resource.string());
                    if (scene.gpu_runtime_context && !model.sprite_gpu->runtime) {
                        auto context = *scene.gpu_runtime_context;
                        context.bitmap_flags |= 0x40000; // 0x1005fe90 sprite constructor with source mip_sprites=2.
                        if (model.sprite_gpu->source_format != assets::ImageSourceFormat::Dds &&
                            model.sprite_gpu->color_key_applied) {
                            // World/bootstrap uploads can precede device creation.
                            // Their keyed cache is not authored input for runtime resizing.
                            const assets::GpuImageUploadOptions options{true, 16, 32, context};
                            const auto opaque = [](std::span<const std::uint8_t> pixels) {
                                for (std::size_t alpha = 3; alpha < pixels.size(); alpha += 4)
                                    if (pixels[alpha] != 255) return false;
                                return true;
                            };
                            if (model.sprite.source_alpha_known && !model.sprite.source_alpha_flag &&
                                model.sprite.source_format == model.sprite_gpu->source_format &&
                                model.sprite.source_bits_per_pixel == model.sprite_gpu->source_bits_per_pixel &&
                                opaque(model.sprite.rgba) && opaque(model.sprite.mip_rgba))
                                *model.sprite_gpu = assets::gpu_image_from_image(model.sprite, options);
                            else
                                *model.sprite_gpu = assets::load_gpu_image(model.resource, model.sprite, options);
                        } else {
                            *model.sprite_gpu = assets::derive_gpu_image_upload(std::move(*model.sprite_gpu), context, true);
                        }
                    }
                    texture_index = reference_texture(scene,
                        add_texture(scene, std::move(*model.sprite_gpu), model.resource.string()));
                    model.sprite_gpu.reset();
                }
                assets::Mesh mesh;
                mesh.vertices.assign(vertices.begin(), vertices.end());
                const auto& gpu = scene.gpu_textures.at(*texture_index);
                const auto texture_width = gpu.runtime ? gpu.runtime->nominal_width : gpu.width;
                const auto texture_height = gpu.runtime ? gpu.runtime->nominal_height : gpu.height;
                const float u_scale = static_cast<float>(model.sprite.width) / texture_width;
                const float v_scale = static_cast<float>(model.sprite.height) / texture_height;
                for (auto& vertex : mesh.vertices) { vertex.u *= u_scale; vertex.v *= v_scale; }
                mesh.indices = {0, 1, 2, 0, 2, 3}; // Original fan; do not reverse again for the backend.
                const auto owned_index = static_cast<std::uint32_t>(scene.owned_meshes.size());
                scene.owned_meshes.push_back(std::move(mesh));
                auto instance = batch(scene.owned_meshes.back(), model.resource.string(), model.material);
                instance.kind = InstanceKind::World;
                instance.source_entity = entity_index;
                instance.source_sprite = true;
                instance.source_sprite_unavailable = SourceSpriteUnavailable::PrivateFlagsAndCachedEulerUnknown;
                instance.flags = flags;
                instance.native_prepared_inactive = entity.native_prepared_inactive;
                instance.texture_index = texture_index;
                instance.source_lighting_cache = &scene.entity_lighting_caches.at(entity_index);
                instance.material_state.entity_ambient_percent = entity.source_ambient.value_or(entity.ambient);
                instance.material_state.entity_albedo_percent = entity.source_albedo.value_or(entity.albedo);
                finish_material(instance, model.material, entity.alpha_percent);
                // Geometry is ready; an actual source per-VIEW state must supply the private cache bits.
                if (!entity.native_prepared_inactive && !(flags & invisible)) {
                    ++scene.counts.omitted_sprite_entities;
                    ++scene.counts.sprite_private_flags_unknown;
                    ++scene.counts.sprite_cached_euler_unknown;
                }
                scene.sprite_bindings.push_back({entity_index, instance.mesh_index, owned_index,
                    model.sprite.width, model.sprite.height, static_cast<std::uint32_t>(scene.instances.size())});
                scene.instances.push_back(std::move(instance));
                continue;
            }
            auto& batches = models.at(entity.model_index);
            if (!batches) {
                if (!model.imported_mesh.indices.empty()) {
                    batches.emplace();
                    auto instance = batch(model.imported_mesh, model.imported_resource.string(), model.material);
                    if (!settings.texture.empty()) instance.texture_index = texture(settings.texture);
                    else if (!model.texture_resource.empty()) instance.texture_index = texture(model.texture_resource);
                    const auto& selected_model = model.morphed_model ? model.morphed_model : model.model;
                    if (selected_model && !selected_model->groups.empty())
                        entity_skin_slots(instance, selected_model->groups.front(), model.skin_resources.front(),
                            (model.morphed_model ? model.morphed_resource : model.resource).string() + "#group=0");
                    batches->push_back(std::move(instance));
                    ++scene.counts.groups;
                } else {
                    const bool morphed = static_cast<bool>(model.morphed_model);
                    auto& selected_model = morphed ? model.morphed_model : model.model;
                    if (!selected_model) throw std::runtime_error("World entity has no original model geometry: " + model.resource.string());
                    const auto source = (morphed ? model.morphed_resource : model.resource).string();
                    auto [domain, fresh] = uv_domains.try_emplace(selected_model);
                    if (fresh) {
                        domain->second = source_uv_domain(*selected_model, model.skin_resources, source);
                        if (!domain->second) domain->second = selected_model;
                    }
                    selected_model = domain->second;
                    batches = model_batches(*selected_model, model.skin_resources, source, model.material,
                        model.texture_resource);
                }
                if (batches->empty()) throw std::runtime_error("World model contains no supported geometry: " + model.resource.string());
                const auto technique = model.effect_resource.empty() ? std::optional<MaterialTechnique>{}
                    : std::optional<MaterialTechnique>{technique_from_path(model.effect_resource)};
                std::array<std::optional<std::uint32_t>, 4> material_skins;
                for (std::size_t slot = 0; slot < material_skins.size(); ++slot)
                    if (!model.material_skin_resources[slot].empty())
                        // 0x100480b0 passes mode0 for MATERIAL bitmaps, independently of ENTITY OVERLAY.
                        material_skins[slot] = cache_source_texture(scene, model.material_skin_resources[slot],
                            (model.material_skin_cube_mask & (1u << slot)) != 0,
                            assets::GpuImageUploadOptions{false, 16, 32, scene.gpu_runtime_context});
                for (auto& instance : *batches) {
                    if (technique) instance.material_state.technique = *technique;
                    instance.material_state.material_skins = material_skins;
                    instance.material_state.material_skins_known_mask = model.material_skins_known_mask;
                }
            }
            if (!entity.native_prepared_inactive && !(flags & invisible)) ++scene.counts.world_instances;
            const auto transform = assets::world_entity_transform(entity);
            for (const auto& original : *batches) {
                auto instance = original;
                instance.model_to_world = transform;
                instance.flags = flags;
                instance.kind = InstanceKind::World;
                instance.source_entity = static_cast<std::uint32_t>(&entity - world.entities.data());
                instance.native_prepared_inactive = entity.native_prepared_inactive;
                instance.source_lighting_cache = &scene.entity_lighting_caches.at(*instance.source_entity);
                instance.source += "#object=" + std::to_string(entity.object_index) + ":" + entity.name;
                instance.material_state.entity_ambient_percent = entity.source_ambient.value_or(entity.ambient);
                instance.material_state.entity_albedo_percent = entity.source_albedo.value_or(entity.albedo);
                if (std::find(world.unresolved_materials.begin(), world.unresolved_materials.end(), instance.material)
                        != world.unresolved_materials.end())
                    instance.material_state.technique = MaterialTechnique::Unknown;
                finish_material(instance, model.material, entity.alpha_percent);
                scene.instances.push_back(std::move(instance));
            }
        }
    }

    void image_surface() {
        const auto image_index = texture(settings.texture);
        const auto& image = scene.textures[image_index];
        const auto& gpu = scene.gpu_textures.at(image_index);
        const float u_extent = gpu.runtime
            ? static_cast<float>(image.width) / gpu.runtime->nominal_width : 1.0f;
        const float v_extent = gpu.runtime
            ? static_cast<float>(image.height) / gpu.runtime->nominal_height : 1.0f;
        const auto half_width = static_cast<float>(image.width) * 0.5f;
        const auto half_height = static_cast<float>(image.height) * 0.5f;
        assets::Mesh surface;
        for (const auto& corner : std::array<std::array<float, 4>, 4>{{
            {-half_width, half_height, 0, 0}, {-half_width, -half_height, 0, 1},
            {half_width, -half_height, 1, 1}, {half_width, half_height, 1, 0}}}) {
            surface.vertices.push_back({camera_right[0]*corner[0] + camera_up[0]*corner[1],
                camera_right[1]*corner[0] + camera_up[1]*corner[1],
                camera_right[2]*corner[0] + camera_up[2]*corner[1], corner[2]*u_extent, corner[3]*v_extent,
                camera_back[0], camera_back[1], camera_back[2]});
        }
        surface.indices = {0, 1, 2, 0, 2, 3};
        scene.owned_meshes.push_back(std::move(surface));
        const auto& mesh = scene.owned_meshes.back();
        auto instance = batch(mesh, settings.texture.string(), "image display surface");
        instance.texture_index = image_index;
        instance.kind = InstanceKind::Image;
        finish_material(instance, {}, 100); // Image display is not an authored WDL material.
        scene.instances.push_back(std::move(instance));
    }

    RenderScene load() {
        if (scene.world || !settings.world.empty()) world_scene();
        else if (!settings.mesh.empty()) {
            auto extension = settings.mesh.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension == ".mdl") {
                scene.model.emplace(assets::load_model(settings.mesh));
                const auto skins = assets::resolve_model_skins(settings.mesh, *scene.model, scene.source_profile);
                if (const auto scaled = source_uv_domain(*scene.model, skins, settings.mesh.string())) *scene.model = *scaled;
                scene.instances = model_batches(*scene.model, skins, settings.mesh.string(), {});
                if (scene.instances.empty())
                    throw std::runtime_error("Selected original model contains no supported triangle geometry: " + settings.mesh.string());
            } else {
                if (!settings.animation.empty()) throw std::runtime_error("Animation requires an original model, not a static mesh");
                scene.owned_meshes.push_back(assets::load_mesh(settings.mesh));
                for (auto& vertex : scene.owned_meshes.back().vertices) {
                    std::swap(vertex.y, vertex.z); // Original bmesh D3D axes to engine Z-up.
                    std::swap(vertex.ny, vertex.nz);
                }
                const auto& mesh = scene.owned_meshes.back();
                auto instance = batch(mesh, settings.mesh.string(), {});
                if (!settings.texture.empty()) instance.texture_index = texture(settings.texture);
                finish_material(instance, {}, 50);
                scene.instances.push_back(std::move(instance));
                ++scene.counts.groups;
            }
        } else if (!settings.texture.empty()) image_surface();
        if (!settings.animation.empty() && !matched_animation)
            throw std::runtime_error("No loaded original model group contains animation: " + settings.animation);
        for (const auto& instance : scene.instances) include_bounds(scene, instance);
        scene.animation = settings.animation;
        for (const auto& binding : scene.pose_bindings) {
            const auto first = assets::animation_sample(*binding.group, scene.animation, 0, false).first_frame;
            const auto last = assets::animation_sample(*binding.group, scene.animation, 100, false).first_frame;
            for (std::uint32_t frame = first; frame <= last; ++frame) {
                assets::sample_pose(*binding.group, {frame, frame, 0}, scene.posed_vertices[binding.vertices_index]);
                for (const auto& instance : scene.instances)
                    if (std::find(binding.mesh_indices.begin(), binding.mesh_indices.end(), instance.mesh_index)
                            != binding.mesh_indices.end())
                        include_bounds(scene, instance, false);
            }
            assets::sample_pose(*binding.group, binding.sample, scene.posed_vertices[binding.vertices_index]);
        }
        // Recovered SHM actions write the shared declared MATERIAL before ent_importmesh.
        // Their successful imported resource, not a guessed normal shader, proves this write.
        if (scene.world) for (const auto& entity : scene.world->entities) {
            if (entity.model_index == assets::no_world_model
                    || scene.world->models.at(entity.model_index).imported_mesh.indices.empty()) continue;
            const auto material = source_imported_mesh_material(entity.action);
            if (material.empty()) continue;
            for (auto& instance : scene.instances)
                if (instance.material == material)
                    apply_source_imported_mesh_tangent_off(instance.material_state, entity.action);
        }
        for (auto& instance : scene.instances) ensure_source_tangents(scene, instance);
        return std::move(scene);
    }
};
} // namespace

RenderScene load_render_scene(const Settings& settings) {
    return Loader(settings).load();
}

RenderScene load_render_scene(const Settings& settings, const assets::GpuImageRuntimeContext& context) {
    return Loader(settings, context).load();
}

RenderScene load_render_scene(const Settings& settings, assets::WorldScene world) {
    return Loader(settings, std::move(world)).load();
}

RenderScene load_render_scene(const Settings& settings, assets::WorldScene world,
    const assets::GpuImageRuntimeContext& context) {
    return Loader(settings, std::move(world), context).load();
}


void prepare_source_sky(RenderScene& scene, const std::filesystem::path& resource_owner) {
    if (scene.sky) throw std::runtime_error("Declared source sky was already prepared");
    const std::array<std::string_view, 1> names{
        scene.source_profile.mtq == 0 ? "sky_blue_a+6.tga" : "sky_blue_c+6.tga"};
    const auto resource = assets::resolve_resources(resource_owner, names, scene.source_profile).at(0);
    const auto strip = assets::load_image(resource);
    assets::GpuImageUploadOptions options;
    options.runtime = scene.gpu_runtime_context;
    if (options.runtime) options.runtime->bitmap_flags |= 0x40000; // Recovered MIP_SPRITES=2: four upload levels.
    auto images = assets::gpu_sky_faces_from_image(strip, options);
    SourceSkyBinding sky;
    sky.resource = resource.string();
    sky.first_face_width = strip.width / 6;
    sky.first_face_height = strip.height;
    // parse_world rejects nonempty surface section6; 10045c80 then seeds10227d00=1.
    // Its only later surface writer1004b660 can set1, never clear it.
    sky.stage_enabled = scene.world && scene.world->source_empty_collision_root_known;
    sky.stage_enabled_known = sky.stage_enabled;
    // 1003c2d0 initializes public B/G/R to128 Q10; this declaration never overrides them.
    sky.source_lighting_cache.source_color_raw_bgr = {131072, 131072, 131072};
    sky.source_lighting_cache.source_color_known = true;
    for (std::size_t face = 0; face < sky.faces.size(); ++face) {
        auto& binding = sky.faces[face];
        binding.texture_index = reference_texture(scene, add_texture(scene, std::move(images[face]),
            sky.resource + "#sky-face=" + std::to_string(face)));
        const auto& gpu = scene.gpu_textures.at(binding.texture_index);
        auto vertices = ui::original_view_sprite_vertices(
            static_cast<float>(sky.first_face_width), static_cast<float>(sky.first_face_height));
        const auto texture_width = gpu.runtime ? gpu.runtime->nominal_width : gpu.width;
        const auto texture_height = gpu.runtime ? gpu.runtime->nominal_height : gpu.height;
        const float u_scale = static_cast<float>(sky.first_face_width) / texture_width;
        const float v_scale = static_cast<float>(sky.first_face_height) / texture_height;
        for (auto& vertex : vertices) { vertex.u *= u_scale; vertex.v *= v_scale; }
        assets::Mesh mesh;
        mesh.vertices.assign(vertices.begin(), vertices.end());
        mesh.indices = {0, 1, 2, 0, 2, 3}; // Original bitmap triangle fan, with culling disabled.
        scene.owned_meshes.push_back(std::move(mesh));
        const auto& stored = scene.owned_meshes.back();
        binding.mesh_index = static_cast<std::uint32_t>(scene.meshes.size());
        scene.meshes.push_back({stored.vertices, stored.indices, 0, stored.secondary_uv, stored.tertiary_uv});
        auto& material = binding.material_state;
        material.source_profile = scene.source_profile;
        material.entity_skins[0] = binding.texture_index;
        // 100379e0 selects the builtin bitmap material, not a source-assigned FX.
        configure_material_state(material, "mat_sprite", sky.authored_flags, 50);
        material.source_material_assigned = false;
        configure_source_bitmap_raster(material, false, gpu.source_alpha_flag);
        // Bitmap stage0 calls10044620(mode0): CLAMP U/V; source filtered MIN/MAG.
        material.entity_samplers[0] = {AddressMode::Clamp, AddressMode::Clamp,
            AddressMode::Unspecified, true, true};
        material.source_pass.sampler = material.entity_samplers[0];
        material.passes[0].sampler = material.entity_samplers[0];
        binding.original_alpha = original_model_alpha_decision(
            {false, 0, 1, gpu.source_alpha_flag, false, 50, false, true, false, 0});
        binding.original_alpha.depth_write = false; // 1004a280 -> 100442a0(2, prior blend).
        material.raster = apply_original_model_alpha(material.raster, binding.original_alpha);
        material.source_pass.raster = material.raster;
        material.passes[0].raster = material.raster;
    }
    scene.sky.emplace(std::move(sky));
}
const assets::ModelGroup& source_model_group(const RenderScene& scene, std::uint32_t entity, std::uint32_t group) {
    if (!scene.world) throw std::runtime_error("Original entity model groups require a prepared world");
    const auto model_index = scene.world->entities.at(entity).model_index;
    if (model_index == assets::no_world_model) throw std::runtime_error("Original entity has no model resource");
    const auto& binding = scene.world->models.at(model_index);
    const auto& model = binding.morphed_model ? binding.morphed_model : binding.model;
    if (!model) throw std::runtime_error("Original entity is not a grouped model");
    return model->groups.at(group);
}

void prepare_entity_animation(RenderScene& scene, std::uint32_t entity) {
    for (const auto& instance : scene.instances) {
        if (instance.source_entity != entity || !instance.source_group) continue;
        const auto group_index = *instance.source_group;
        const auto& group = source_model_group(scene, entity, group_index);
        if (group.frames.size() <= 1) continue;
        if (std::any_of(scene.pose_bindings.begin(), scene.pose_bindings.end(), [&](const auto& binding) {
                return binding.source_entity == entity && binding.group_index == group_index;
            })) continue;
        const auto& source = scene.meshes.at(instance.mesh_index);
        const auto vertices_index = scene.posed_vertices.size();
        scene.posed_vertices.emplace_back(source.vertices.begin(), source.vertices.end());
        PoseBinding binding{&group, vertices_index, {}, {}, entity, group_index, false};
        const auto tangent_index = scene.tangent_bindings.size();
        scene.tangent_bindings.push_back({scene.posed_vertices.back(), group.mesh.indices});
        binding.tangent_binding = tangent_index;
        for (auto& draw : scene.instances) {
            if (draw.source_entity != entity || draw.source_group != group_index) continue;
            auto view = scene.meshes.at(draw.mesh_index);
            view.vertices = scene.posed_vertices.back();
            view.pose_revision = 0;
            view.tertiary_uv = group.mesh.tertiary_uv;
            view.tangent_provenance = group.mesh.tangent_provenance;
            view.tangent_binding = tangent_index;
            if (draw.source_tangent_requested) {
                auto& prior = scene.tangent_bindings.at(*scene.meshes.at(draw.mesh_index).tangent_binding);
                --prior.request_count;
                draw.source_tangent_requested = false;
            }
            draw.mesh_index = static_cast<std::uint32_t>(scene.meshes.size());
            scene.meshes.push_back(view);
            binding.mesh_indices.push_back(draw.mesh_index);
            scene.tangent_bindings[tangent_index].mesh_indices.push_back(draw.mesh_index);
        }
        scene.pose_bindings.push_back(std::move(binding));
        for (auto& draw : scene.instances)
            if (draw.source_entity == entity && draw.source_group == group_index)
                ensure_source_tangents(scene, draw);
    }
}

void sample_entity_pose(RenderScene& scene, std::uint32_t entity, std::uint32_t group,
    assets::AnimationSample sample) {
    for (auto& binding : scene.pose_bindings)
        if (binding.source_entity == entity && binding.group_index == group) {
            update_pose_binding(scene, binding, sample);
            return;
        }
    if (source_model_group(scene, entity, group).frames.size() <= 1) return;
    throw std::runtime_error("Original entity animation buffers were not prepared before rendering");
}

void ensure_source_tangents(RenderScene& scene, Instance& instance) {
    const auto& mesh = scene.meshes.at(instance.mesh_index);
    if (!mesh.tangent_binding) return;
    auto& binding = scene.tangent_bindings.at(*mesh.tangent_binding);
    const bool requested = source_material_tangent_request(&instance.material_state).value_or(false);
    if (requested != instance.source_tangent_requested) {
        if (requested) ++binding.request_count;
        else --binding.request_count;
        instance.source_tangent_requested = requested;
    }
    if (requested && !binding.generated) generate_binding_tangents(scene, binding);
}

std::uint32_t cache_texture(RenderScene& scene, const std::filesystem::path& path, bool cube) {
    return cache_source_texture(scene, path, cube,
        assets::GpuImageUploadOptions{false, 16, 32, scene.gpu_runtime_context});
}

std::uint32_t cache_model_skin(RenderScene& scene, std::uint32_t entity, std::uint32_t group_index,
    std::uint32_t skin_index) {
    const auto& group = source_model_group(scene, entity, group_index);
    const auto& model = scene.world->models.at(scene.world->entities.at(entity).model_index);
    const auto& resource = model.skin_resources.at(group_index).at(skin_index);
    const auto& entity_state = scene.world->entities.at(entity);
    assets::GpuImageUploadOptions options;
    options.runtime = scene.gpu_runtime_context;
    options.overlay = (((entity_state.flags | entity_state.source_flags_set) & ~entity_state.source_flags_clear)
        & (1u << 12)) != 0;
    if (!resource.empty()) return cache_source_texture(scene, resource, false, options);
    const auto source = (model.morphed_model ? model.morphed_resource : model.resource).string()
        + "#group=" + std::to_string(group_index) + ":" + group.name + "#skin=" + std::to_string(skin_index);
    for (std::size_t i = 0; i < scene.texture_sources.size(); ++i)
        if (scene.texture_sources[i] == source)
            return reference_texture(scene, static_cast<std::uint32_t>(i));
    const auto& skin = group.skins.at(skin_index);
    const auto palette = skin.format == 0 ? skin_palette(scene) : std::span<const std::uint8_t>{};
    return reference_texture(scene, add_texture(scene,
        assets::gpu_image_from_skin(skin, options, palette), source));
}

void configure_instance_material(RenderScene& scene, Instance& instance,
    std::string_view material_name, float alpha_percent) {
    instance.material_state.entity_skins[0] = instance.texture_index;
    instance.source_alpha_percent = alpha_percent;
    instance.material_state.source_profile = scene.world ? scene.world->source_profile : scene.source_profile;
    configure_material_state(instance.material_state, material_name, instance.flags, alpha_percent);
    if (!material_name.empty() && !instance.material_state.source_material_initialized)
        initialize_source_material_event_state(instance.material_state, material_name);
    if (scene.world && instance.source_entity) {
        const auto& entity = scene.world->entities.at(*instance.source_entity);
        if (entity.model_index != assets::no_world_model
                && !scene.world->models.at(entity.model_index).imported_mesh.indices.empty()
                && material_name == source_imported_mesh_material(entity.action))
            apply_source_imported_mesh_tangent_off(instance.material_state, entity.action);
    }
    if (const auto texture = instance.material_state.entity_skins[0]) {
        const auto& bitmap = scene.gpu_textures.at(*texture);
        if (bitmap.source_alpha_known)
            configure_source_bitmap_raster(instance.material_state, (instance.flags & 0x1000) != 0,
                bitmap.source_alpha_flag);
    } else {
        configure_source_bitmap_raster(instance.material_state, (instance.flags & 0x1000) != 0, false);
    }
    // Original OVERLAY keying is already applied to the first-upload GPU bytes.
    // Fragment RGB equality after filtering is not the original bitmap operation.
    instance.material_state.raster.color_key = false;
    for (std::size_t pass = 0; pass < instance.material_state.pass_count; ++pass)
        instance.material_state.passes[pass].raster.color_key = false;
}

void capture_original_root_alpha(RenderScene& scene) {
    if (scene.world) {
        scene.counts.world_entities = 0;
        scene.counts.sprite_entities = 0;
        for (const auto& entity : scene.world->entities) {
            if (entity.native_prepared_inactive) continue;
            ++scene.counts.world_entities;
            if (entity.model_index != assets::no_world_model
                    && !scene.world->models.at(entity.model_index).sprite.rgba.empty())
                ++scene.counts.sprite_entities;
        }
    }
    for (auto& instance : scene.instances) {
        instance.original_alpha.reset();
        bool bitmap_alpha = false;
        if (const auto texture = instance.material_state.entity_skins[0]) {
            const auto& bitmap = scene.gpu_textures.at(*texture);
            if (!bitmap.source_alpha_known) continue;
            bitmap_alpha = bitmap.source_alpha_flag;
        }
        configure_source_bitmap_raster(instance.material_state, (instance.flags & 0x1000) != 0, bitmap_alpha);
        if (!instance.source_skin_override_known) continue;
        const auto alpha_percent = instance.source_alpha_percent;
        instance.original_alpha = original_model_alpha_decision({
            instance.source_skin_ambient_alpha.has_value(), instance.source_skin_ambient_alpha.value_or(0),
            1.0f, bitmap_alpha, (instance.flags & 0x400) != 0, alpha_percent,
            (instance.flags & 0x200000) != 0, true, false, 0});
    }
    ++scene.source_draw_revision;
}

void synchronize_source_sprite(RenderScene& scene, std::uint32_t entity,
    const SourceSpritePose& pose, const SourceSpriteView& view) {
    constexpr std::uint32_t required_private = 0x520000;
    const auto binding = std::lower_bound(scene.sprite_bindings.begin(), scene.sprite_bindings.end(), entity,
        [](const auto& value, std::uint32_t id) { return value.source_entity < id; });
    if (binding == scene.sprite_bindings.end() || binding->source_entity != entity)
        throw std::runtime_error("Source sprite geometry was not prepared before GPU upload");
    auto& instance = scene.instances.at(binding->instance_index);
    const bool private_flags_known = (pose.internal_flags_known_mask & required_private) == required_private;
    if (!private_flags_known || !pose.cached_angles_known) {
        instance.source_sprite_unavailable = !private_flags_known
            ? (pose.cached_angles_known ? SourceSpriteUnavailable::PrivateFlagsUnknown
                : SourceSpriteUnavailable::PrivateFlagsAndCachedEulerUnknown)
            : SourceSpriteUnavailable::CachedEulerUnknown;
        instance.source_sprite_state_known = false;
        instance.flags = pose.authored_flags;
        instance.native_prepared_inactive = scene.world->entities.at(entity).native_prepared_inactive;
        binding->state_cached = false;
        binding->cached_angles_after_draw_known = false;
        return; // Report the omitted source sprite; do not retain a stale guessed matrix as visible.
    }
    if (binding->state_cached && binding->previous_pose == pose && binding->previous_view == view
            && instance.native_prepared_inactive == scene.world->entities.at(entity).native_prepared_inactive)
        return;
    auto& mesh = scene.owned_meshes.at(binding->owned_mesh_index);
    const float hw = static_cast<float>(static_cast<double>(binding->width) * pose.scale[0] * 0.5);
    const float hh = static_cast<float>(static_cast<double>(binding->height) * pose.scale[1] * 0.5);
    const std::array<float, 4> ys{-hw, hw, hw, -hw}, zs{hh, hh, -hh, -hh};
    bool changed = false;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        changed |= mesh.vertices[i].y != ys[i] || mesh.vertices[i].z != zs[i];
        mesh.vertices[i].y = ys[i];
        mesh.vertices[i].z = zs[i];
    }
    if (changed) {
        auto& revision = scene.meshes.at(binding->mesh_index).pose_revision;
        if (revision == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Scene sprite pose revision exhausted");
        ++revision;
    }
    const bool oriented = (pose.authored_flags & 0x400000) != 0
        || pose.authored_angles[0] != 0 || pose.authored_angles[1] != 0
        || (pose.internal_flags & 0x100000) != 0;
    const bool roll_facing = pose.authored_angles[2] != 0
        && pose.authored_angles[0] == 0 && pose.authored_angles[1] == 0;
    assets::WorldEntity transform;
    transform.position = pose.origin;
    transform.angles = pose.cached_angles;
    transform.scale = {1, 1, 1}; // Original sprite scale was baked into the corners.
    if (!oriented || roll_facing) {
        if (pose.internal_flags & 0x400000) transform.angles[0] += 180;
        else {
            transform.angles[0] = view.pan + 180;
            if (roll_facing) {
                const auto encoded = static_cast<std::int64_t>(std::trunc(
                    static_cast<double>(view.tilt) * 182.04444444444445));
                const auto low = static_cast<std::uint16_t>(static_cast<std::uint64_t>(encoded));
                const auto signed_low = low < 0x8000 ? static_cast<std::int32_t>(low)
                    : static_cast<std::int32_t>(low) - 0x10000;
                float angle = static_cast<float>(static_cast<double>(signed_low) * 0.0054931640625);
                if (angle > 180) angle -= 360;
                else if (angle < -180) angle += 360;
                transform.angles[1] = -angle;
            } else transform.angles[1] = 0;
        }
    }
    binding->cached_angles_after_draw = transform.angles;
    binding->cached_angles_after_draw_known = true;
    const auto matrix = assets::world_entity_transform(transform);
    instance.model_to_world = matrix;
    instance.flags = pose.authored_flags | ((pose.internal_flags & 0x20000) ? 0u : 0x1000u);
    instance.native_prepared_inactive = scene.world->entities.at(entity).native_prepared_inactive;
    instance.source_sprite_no_depth_write = (pose.internal_flags & 0x20000) != 0;
    instance.source_cull_mode = !(pose.authored_flags & 0x400000) ? 1u
        : view.focal_x * view.focal_y > 0 && !(view.flags & 0x20000) ? 3u : 2u;
    instance.source_sprite_state_known = true;
    instance.source_sprite_unavailable = SourceSpriteUnavailable::None;
    const auto texture_transform = instance.material_state.texture_transform;
    const auto bump_matrix = instance.material_state.bump_environment_matrix;
    const auto bump_known = instance.material_state.bump_matrix_known;
    const auto& entity_state = scene.world->entities.at(entity);
    instance.material_state.entity_ambient_percent = entity_state.source_ambient.value_or(entity_state.ambient);
    instance.material_state.entity_albedo_percent = entity_state.source_albedo.value_or(entity_state.albedo);
    configure_instance_material(scene, instance, instance.material, instance.source_alpha_percent);
    instance.material_state.texture_transform = texture_transform;
    instance.material_state.bump_environment_matrix = bump_matrix;
    instance.material_state.bump_matrix_known = bump_known;
    binding->previous_pose = pose;
    // Without the additive private branch, another draw with this returned cache is idempotent.
    if (!(pose.internal_flags & 0x400000)) binding->previous_pose.cached_angles = transform.angles;
    binding->previous_view = view;
    binding->state_cached = true;
}

void sample_selected_animation(RenderScene& scene, float percentage) {
    for (auto& binding : scene.pose_bindings) {
        if (binding.source_entity) continue;
        update_pose_binding(scene, binding,
            assets::animation_sample(*binding.group, scene.animation, percentage, false));
    }
}

SceneCounts submitted_scene_counts(const FrameSnapshot& frame) {
    auto counts = frame.scene.counts;
    counts.mesh_vertices = counts.mesh_triangles = counts.world_vertices = counts.world_triangles = 0;
    counts.world_instances = 0;
    counts.omitted_sprite_entities = 0;
    counts.sprite_private_flags_unknown = counts.sprite_cached_euler_unknown = 0;
    counts.hidden_entities = frame.scene.hidden_unrenderable_entities;
    if (frame.overlay && !frame.overlay->draw_world) return counts;
    std::optional<std::uint32_t> previous_entity;
    for (const auto& instance : frame.instances) {
        if (instance.native_prepared_inactive) continue;
        const bool visible = instance_visible(instance);
        if (instance.source_sprite && !(instance.flags & invisible) && !instance.source_sprite_state_known) {
            ++counts.omitted_sprite_entities;
            const auto reason = instance.source_sprite_unavailable;
            if (reason == SourceSpriteUnavailable::PrivateFlagsUnknown
                    || reason == SourceSpriteUnavailable::PrivateFlagsAndCachedEulerUnknown)
                ++counts.sprite_private_flags_unknown;
            if (reason == SourceSpriteUnavailable::CachedEulerUnknown
                    || reason == SourceSpriteUnavailable::PrivateFlagsAndCachedEulerUnknown)
                ++counts.sprite_cached_euler_unknown;
        }
        if (instance.kind == InstanceKind::World && instance.source_entity != previous_entity) {
            previous_entity = instance.source_entity;
            if (visible) ++counts.world_instances;
            else if (instance.flags & invisible) ++counts.hidden_entities;
        }
        if (!visible) continue;
        const auto& mesh = frame.scene.meshes.at(instance.mesh_index);
        counts.mesh_vertices += mesh.vertices.size();
        counts.mesh_triangles += mesh.indices.size() / 3;
        if (instance.kind == InstanceKind::World) {
            counts.world_vertices += mesh.vertices.size();
            counts.world_triangles += mesh.indices.size() / 3;
        }
    }
    return counts;
}

CameraState inspection_camera(const RenderScene& scene, int width, int height) {
    if (width <= 0 || height <= 0) throw std::runtime_error("Inspection camera needs a positive viewport");
    CameraState camera{identity, "native Z-up orthographic inspection camera; not the original game camera"};
    if (!scene.has_bounds) return camera;
    std::array<float, 3> low{}, high{};
    for (unsigned corner = 0; corner < 8; ++corner) {
        std::array<float, 3> point{};
        for (unsigned axis = 0; axis < 3; ++axis)
            point[axis] = (corner & (1u << axis)) ? scene.bounds_max[axis] : scene.bounds_min[axis];
        const std::array<float, 3> projected{
            point[0]*camera_right[0] + point[1]*camera_right[1] + point[2]*camera_right[2],
            point[0]*camera_up[0] + point[1]*camera_up[1] + point[2]*camera_up[2],
            point[0]*camera_back[0] + point[1]*camera_back[1] + point[2]*camera_back[2]};
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(projected[axis])) throw std::runtime_error("Inspection camera bounds overflow");
            if (corner == 0) low[axis] = high[axis] = projected[axis];
            else { low[axis] = std::min(low[axis], projected[axis]); high[axis] = std::max(high[axis], projected[axis]); }
        }
    }
    const auto aspect = static_cast<float>(width) / static_cast<float>(height);
    float half_x = (high[0] - low[0]) * 0.55f;
    float half_y = (high[1] - low[1]) * 0.55f;
    const float size = std::max(half_x, half_y);
    if (!(size > 0)) throw std::runtime_error("Inspection geometry has no projected extent");
    half_y = std::max(half_y, half_x / aspect);
    half_x = half_y * aspect;
    const float half_z = std::max((high[2] - low[2]) * 0.55f, size * 0.01f);
    if (!std::isfinite(half_x) || !std::isfinite(half_y) || !std::isfinite(3*half_z))
        throw std::runtime_error("Inspection camera extent exceeds float range");
    const std::array<float, 3> center{low[0] + (high[0]-low[0])*0.5f,
        low[1] + (high[1]-low[1])*0.5f, low[2] + (high[2]-low[2])*0.5f};
    // Explicit inspection eye outside the bounds; near/far distances are half_z and 3*half_z.
    for (std::size_t axis = 0; axis < 3; ++axis)
        camera.view_position[axis] = camera_right[axis]*center[0] + camera_up[axis]*center[1]
            + camera_back[axis]*(center[2] + 2*half_z);
    camera.view = {camera_right[0], camera_up[0], camera_back[0], 0,
        camera_right[1], camera_up[1], camera_back[1], 0,
        camera_right[2], camera_up[2], camera_back[2], 0,
        -center[0], -center[1], -(center[2] + 2*half_z), 1};
    camera.projection = {1/half_x, 0, 0, 0, 0, 1/half_y, 0, 0,
        0, 0, -1/half_z, 0, 0, 0, -2, 1};
    camera.inverse_projection = {half_x, 0, 0, 0, 0, half_y, 0, 0,
        0, 0, -half_z, 0, 0, 0, -2*half_z, 1};
    camera.clip_near = half_z;
    camera.clip_far = 3*half_z;
    camera.projection_known = true;
    camera.view_projection = {camera_right[0]/half_x, camera_up[0]/half_y, -camera_back[0]/half_z, 0,
        camera_right[1]/half_x, camera_up[1]/half_y, -camera_back[1]/half_z, 0,
        camera_right[2]/half_x, camera_up[2]/half_y, -camera_back[2]/half_z, 0,
        -center[0]/half_x, -center[1]/half_y, center[2]/half_z, 1};
    return camera;
}
} // namespace c4::rendering
