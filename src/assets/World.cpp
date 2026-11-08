#include "World.hpp"
#include "Resources.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <stdexcept>
#include <fstream>
#include <iterator>
#include <string_view>

namespace c4::assets {
namespace {
[[noreturn]] void fail(std::string_view reason) {
    throw std::runtime_error("WMB7 world: " + std::string(reason));
}
struct Reader {
    std::span<const std::uint8_t> bytes;
    void need(std::size_t offset, std::size_t size) const {
        if (offset > bytes.size() || size > bytes.size() - offset) fail("truncated section or record");
    }
    std::uint32_t u32(std::size_t offset) const {
        need(offset, 4);
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset+1]) << 8)
             | (std::uint32_t(bytes[offset+2]) << 16) | (std::uint32_t(bytes[offset+3]) << 24);
    }
    float number(std::size_t offset) const {
        const float value = std::bit_cast<float>(u32(offset));
        if (!std::isfinite(value)) fail("nonfinite object property");
        return value;
    }
    std::string text(std::size_t offset, std::size_t width) const {
        need(offset, width);
        const auto begin = bytes.begin() + offset;
        const auto end = std::find(begin, begin + width, std::uint8_t(0));
        if (std::any_of(begin, end, [](std::uint8_t c) { return c < 32 || c == 127; }))
            fail("control character in object string");
        return std::string(begin, end);
    }
};
struct Section { std::size_t offset{}, length{}; };
std::string lowercase(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return text;
}
bool resource_name(std::string_view name) {
    return !name.empty() && name != "." && name.find("..") == std::string_view::npos
        && name.find_first_of("/\\:") == std::string_view::npos;
}
void validate_profile(const SourceProfile& profile) {
    if (!valid_source_profile(profile)) fail("invalid source profile");
}
struct SourceAlias {
    std::string_view name;
    unsigned axis;
    std::array<std::string_view, 4> values;
};
constexpr SourceAlias source_aliases[] = {
#define WORLD_SOURCE_ALIAS(name, axis, v0, v1, v2, v3) {name, axis, {v0, v1, v2, v3}},
#include "WorldSourceAliases.inc"
#undef WORLD_SOURCE_ALIAS
};
std::string_view resolve_source_name(std::string_view name, const SourceProfile& profile) {
    if (!resource_name(name)) fail("unsafe referenced resource name");
    const auto alias = std::find_if(std::begin(source_aliases), std::end(source_aliases),
        [name](const SourceAlias& item) { return item.name == name; });
    if (alias == std::end(source_aliases)) return name;
    unsigned selector{};
    switch (alias->axis) {
    case 0: break;
    case 1: selector = profile.atq; break;
    case 2: selector = profile.mtq; break;
    case 3: selector = profile.stq; break;
    default: fail("invalid authored source alias axis");
    }
    const auto selected = alias->values[selector];
    if (!resource_name(selected))
        fail("missing source alias definition '" + std::string(name) + "'");
    return selected;
}
bool selected_quality_archive(std::string_view stem, const SourceProfile& profile) {
    for (const auto& [prefix, selector] :
            std::array<std::pair<std::string_view, unsigned>, 3>{{
                {"kaynak_atq", profile.atq}, {"kaynak_mtq", profile.mtq}, {"kaynak_stq", profile.stq}}}) {
        if (!stem.starts_with(prefix)) continue;
        if (prefix == "kaynak_stq" && selector == 0) return false;
        return stem.size() == prefix.size() + 1 && stem.back() == char('0' + selector);
    }
    if (stem.starts_with("rca") && stem.size() == 4 && stem.back() >= '0' && stem.back() <= '1')
        return stem.back() == char('0' + profile.bloom);
    return true;
}
unsigned source_archive_rank(std::string_view stem) {
    if (stem == "c4") return 0; // Engine host bundle mounts before authored resources.
    if (stem == "kaynak_snd") return 1;
    if (stem == "kaynak_bit") return 2;
    if (stem == "kaynak_kap") return 3;
    if (stem.starts_with("kaynak_atq")) return 4;
    if (stem.starts_with("kaynak_stq")) return 5;
    if (stem.starts_with("kaynak_mtq")) return 6;
    if (stem == "rca0" || stem == "rca1") return 7;
    return 8;
}
using Catalog = std::map<std::string, std::filesystem::path>;
Catalog resource_catalog(const std::filesystem::path& owner, const SourceProfile& profile) {
    const auto text = owner.string();
    const auto separator = text.find("::");
    const std::filesystem::path container(text.substr(0, separator));
    auto directory = container.parent_path();
    if (directory.empty()) directory = ".";
    Catalog catalog;
    struct Archive { std::filesystem::path path; unsigned rank; };
    std::vector<Archive> archives;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file()) files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        const auto extension = lowercase(path.extension().string());
        if (extension == ".wrs" || extension == ".rc4") {
            const auto stem = lowercase(path.stem().string());
            if (!selected_quality_archive(stem, profile)) continue;
            // Loose imported bmesh files also use .rc4; only actual WRS3 signatures
            // are archives. Reading just their prefix avoids decoding every asset.
            std::ifstream input(path, std::ios::binary);
            std::array<char, 4> prefix{};
            if (!input.read(prefix.data(), prefix.size())) fail("cannot read neighboring resource header");
            if (prefix == std::array<char, 4>{'W','R','S','3'}) {
                const auto rank = source_archive_rank(stem);
                if (rank != 8 || path.filename() == container.filename()) archives.push_back({path, rank});
                continue;
            }
        }
        catalog.emplace(lowercase(path.filename().string()), path);
    }
    // Original archive lookup is first-mounted-wins: host, common resources,
    // selected ATQ/STQ/MTQ, then any explicitly selected nonquality owner archive.
    // Loose resources retain precedence; unmounted neighbors never supply fallback.
    std::sort(archives.begin(), archives.end(), [](const Archive& a, const Archive& b) {
        return a.rank == b.rank ? a.path < b.path : a.rank < b.rank;
    });
    for (const auto& archive : archives)
        for (const auto& entry : list_resources(archive.path))
            catalog.emplace(lowercase(entry.name), archive.path.string() + "::" + entry.name);
    return catalog;
}
std::filesystem::path resolve(const Catalog& catalog, std::string_view name) {
    if (!resource_name(name)) fail("unsafe referenced resource name");
    const auto found = catalog.find(lowercase(std::string(name)));
    if (found == catalog.end()) fail("missing original resource '" + std::string(name) + "'");
    return found->second;
}
std::filesystem::path resolve_source_resource(
    const Catalog& catalog, std::string_view name, const SourceProfile& profile) {
    return resolve(catalog, resolve_source_name(name, profile));
}
std::vector<std::vector<std::filesystem::path>> skin_paths(
    const Catalog& catalog, const Model& model, const SourceProfile& profile) {
    std::vector<std::vector<std::filesystem::path>> paths;
    paths.reserve(model.groups.size());
    for (const auto& group : model.groups) {
        auto& group_paths = paths.emplace_back(group.skins.size());
        for (std::size_t i = 0; i < group.skins.size(); ++i)
            if (!group.skins[i].external_texture.empty())
                group_paths[i] = resolve_source_resource(catalog, group.skins[i].external_texture, profile);
    }
    return paths;
}
struct Binding {
    std::string_view action, model, imported, texture, material;
    std::uint32_t flags_set, flags_clear;
    std::string_view unsupported;
};
constexpr Binding bindings[] = {
#define WORLD_BINDING(action, model, imported, texture, material, set, clear, unsupported) \
    {action, model, imported, texture, material, set, clear, unsupported},
#include "WorldBindings.inc"
#undef WORLD_BINDING
};
static_assert([] {
    for (std::size_t i = 1; i < std::size(bindings); ++i)
        if (bindings[i-1].action >= bindings[i].action) return false;
    return true;
}(), "World action bindings must be unique and sorted");
// Direct static my.material writes before wait1, not disk MATERIAL or helper results:
// effect_define.wdl194-208; c4_materials.wdl7421-7585 (all 17 shadow actions).
constexpr std::array<std::string_view,19> material_before_wait_actions{
    "GMA_anadolu", "GMA_avrupa",
    "SHM_anayol", "SHM_beton", "SHM_cikis", "SHM_denekop_ek", "SHM_ex_yol",
    "SHM_ex_yol_kald", "SHM_gecis", "SHM_kule", "SHM_kule2", "SHM_parca_2",
    "SHM_tup_kald", "SHM_tup_yol", "SHM_vadi", "SHM_vadi_yan", "SHM_y_kum_yan",
    "SHM_yol_kum", "SHM_zgisealt"};
static_assert([] {
    for(const auto action:material_before_wait_actions) {
        const auto found=std::find_if(std::begin(bindings),std::end(bindings),
            [action](const Binding& binding){return binding.action==action;});
        if(found==std::end(bindings)||found->material.empty())return false;
    }
    return true;
}(), "Direct source MATERIAL writes require recovered action bindings");
using ProfileMasks = std::array<unsigned, 6>;
bool matches_profile(const ProfileMasks& masks, const SourceProfile& profile) {
    return (masks[0] & (1U << profile.atq)) && (masks[1] & (1U << profile.mtq))
        && (masks[2] & (1U << profile.stq)) && (masks[3] & (1U << profile.cgm))
        && (masks[4] & (1U << profile.shq)) && (masks[5] & (1U << profile.bloom));
}
struct RegisteredMaterial {
    std::string_view name;
    unsigned bloom_mask;
};
constexpr RegisteredMaterial registered_materials[] = {
#define WORLD_REGISTERED_MATERIAL(name, bloom) {name, bloom},
#define WORLD_MATERIAL_RESOURCE(name)
#include "WorldMaterialRegistry.inc"
#undef WORLD_REGISTERED_MATERIAL
#undef WORLD_MATERIAL_RESOURCE
};
constexpr std::string_view authored_entity_resources[] = {
#define WORLD_REGISTERED_MATERIAL(name, bloom)
#define WORLD_MATERIAL_RESOURCE(name) name,
#include "WorldMaterialRegistry.inc"
#undef WORLD_MATERIAL_RESOURCE
#undef WORLD_REGISTERED_MATERIAL
};
constexpr char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}
constexpr bool equal_material_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
}
std::string_view registered_material(std::string_view name, const SourceProfile& profile) {
    const auto found = std::find_if(std::begin(registered_materials), std::end(registered_materials),
        [name, &profile](const RegisteredMaterial& registered) {
            return (registered.bloom_mask & (1U << profile.bloom))
                && equal_material_name(name, registered.name);
        });
    return found == std::end(registered_materials) ? std::string_view{} : found->name;
}
bool authored_bare_resource(std::string_view name) {
    if (std::any_of(std::begin(authored_entity_resources), std::end(authored_entity_resources),
            [name](std::string_view resource) { return equal_material_name(name, resource); }))
        return true;
    for (const auto& alias : source_aliases)
        for (const auto value : alias.values)
            if (!value.empty() && equal_material_name(name, value)) return true;
    return false;
}
struct ProfileBinding {
    ProfileMasks masks;
    Binding binding;
};
constexpr ProfileBinding profile_bindings[] = {
#define WORLD_PROFILE_ACTION(atq, mtq, stq, cgm, shq, bloom, action, model, imported, texture, material, set, clear, unsupported) \
    {{atq, mtq, stq, cgm, shq, bloom}, {action, model, imported, texture, material, set, clear, unsupported}},
#include "WorldActionProfileBindings.inc"
#undef WORLD_PROFILE_ACTION
};
struct Variant {
    std::string_view action;
    std::uint32_t mask, value;
    std::string_view model, texture;
};
constexpr Variant variants[] = {
#define WORLD_VARIANT(action, mask, value, model, texture) {action, mask, value, model, texture},
#include "WorldVariantBindings.inc"
#undef WORLD_VARIANT
};
Binding action_binding(const WorldEntity& entity, const SourceProfile& profile) {
    if (entity.action.empty()) return {};
    const auto found = std::lower_bound(std::begin(bindings), std::end(bindings),
        std::string_view(entity.action), [](const Binding& binding, std::string_view action) {
            return binding.action < action;
        });
    if (found == std::end(bindings) || found->action != entity.action)
        fail("unrecovered entity action '" + entity.action + "'");
    auto binding = *found;
    for (const auto& selected : profile_bindings) {
        if (selected.binding.action != entity.action || !matches_profile(selected.masks, profile)) continue;
        binding = selected.binding;
        break;
    }
    for (const auto& variant : variants) {
        if (variant.action != entity.action || (entity.flags & variant.mask) != variant.value) continue;
        if (!variant.model.empty()) binding.model = variant.model;
        if (!variant.texture.empty()) binding.texture = variant.texture;
        break;
    }
    return binding;
}
struct MaterialBinding {
    std::string_view name;
    std::array<std::string_view, 4> skins;
    std::string_view effect;
};
constexpr MaterialBinding materials[] = {
#define WORLD_MATERIAL(name, skin1, skin2, skin3, skin4, effect) \
    {name, {skin1, skin2, skin3, skin4}, effect},
#include "WorldMaterialBindings.inc"
#undef WORLD_MATERIAL
};
struct ProfileMaterialBinding {
    unsigned quality_mask, shadow_mask;
    MaterialBinding binding;
};
constexpr ProfileMaterialBinding profile_materials[] = {
#define WORLD_PROFILE_MATERIAL(shq, shadow, name, skin1, skin2, skin3, skin4, effect) \
    {shq, shadow, {name, {skin1, skin2, skin3, skin4}, effect}},
#include "WorldMaterialProfileBindings.inc"
#undef WORLD_PROFILE_MATERIAL
};
constexpr ProfileMaterialBinding profile_terrain_materials[] = {
#define WORLD_PROFILE_TERRAIN(mtq, shadow, name, skin1, skin2, skin3, skin4, effect) \
    {mtq, shadow, {name, {skin1, skin2, skin3, skin4}, effect}},
#include "WorldTerrainProfileBindings.inc"
#undef WORLD_PROFILE_TERRAIN
};
// effect_define.wdl:9-20,42-45,67-78: exact authored cube bitmap identities.
constexpr std::array<std::string_view, 12> material_cube_aliases{
    "bcube00_bmap", "bcube01_bmap", "bcube02_bmap", "bcube03_bmap",
    "bcube04_bmap", "bcube05_bmap", "bcube06_bmap", "bcube07_bmap",
    "bcube08_bmap", "bcube09_bmap", "bcube10_bmap", "bcube11_bmap"};
WorldMaterialResources resolve_material(
    const Catalog& catalog, std::string_view name, const SourceProfile& profile) {
    const auto baseline = std::find_if(std::begin(materials), std::end(materials),
        [name](const MaterialBinding& item) { return item.name == name; });
    if (baseline == std::end(materials)) fail("unrecovered material '" + std::string(name) + "'");
    const MaterialBinding* material = &*baseline;
    const auto shadow_bit = 1U << (profile.stq > 0);
    for (const auto& selected : profile_materials)
        if (selected.binding.name == name && (selected.quality_mask & (1U << profile.shq))
            && (selected.shadow_mask & shadow_bit)) {
            material = &selected.binding;
            break;
        }
    for (const auto& selected : profile_terrain_materials)
        if (selected.binding.name == name && (selected.quality_mask & (1U << profile.mtq))
            && (selected.shadow_mask & shadow_bit)) {
            material = &selected.binding;
            break;
        }
    WorldMaterialResources resources;
    resources.name = material->name;
    resources.definition_known = name != "Mtl_ENGEL_ORTA" && name != "Mtl_ShaderEditor";
    resources.material_skins_known_mask = resources.definition_known ? 0xf : 0;
    for (std::size_t slot = 0; slot < 4; ++slot)
        if (!material->skins[slot].empty()) {
            resources.skins[slot] = resolve_source_resource(catalog, material->skins[slot], profile);
            if (resources.definition_known &&
                std::find(material_cube_aliases.begin(), material_cube_aliases.end(), material->skins[slot])
                    != material_cube_aliases.end())
                resources.material_skin_cube_mask |= static_cast<std::uint8_t>(1U << slot);
        }
    if (!material->effect.empty())
        resources.effect = resolve_source_resource(catalog, material->effect, profile);
    return resources;
}
struct Appearance {
    std::string_view action;
    std::optional<float> alpha, ambient, albedo;
};
constexpr Appearance appearances[] = {
#define WORLD_APPEARANCE(action, alpha, ambient, albedo) {action, alpha, ambient, albedo},
#include "WorldAppearanceBindings.inc"
#undef WORLD_APPEARANCE
};
Appearance action_appearance(std::string_view action) {
    const auto found = std::find_if(std::begin(appearances), std::end(appearances),
        [action](const Appearance& item) { return item.action == action; });
    return found == std::end(appearances) ? Appearance{} : *found;
}
}

WorldScene parse_world(std::span<const std::uint8_t> bytes) {
    Reader file{bytes};
    file.need(0, 156);
    if (std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) != "WMB7")
        fail("unsupported signature");
    // Original compiler 7.01 writes nineteen lists, not the later 7.70 twenty-list dialect.
    std::array<Section, 19> sections{};
    for (std::size_t i = 0; i < sections.size(); ++i) {
        auto& section = sections[i];
        section = {file.u32(4 + i*8), file.u32(8 + i*8)};
        if ((section.offset == 0) != (section.length == 0)) fail("inconsistent empty section");
        if (!section.length) continue;
        if (section.offset < 156) fail("section overlaps header");
        file.need(section.offset, section.length);
        for (std::size_t j = 0; j < i; ++j) {
            const auto& previous = sections[j];
            if (previous.length && section.offset < previous.offset + previous.length
                && previous.offset < section.offset + section.length) fail("overlapping sections");
        }
    }
    // Do not silently interpret a later twenty-list header and ignore its terrain list.
    if (std::none_of(sections.begin(), sections.end(), [](const Section& section) {
            return section.length && section.offset == 156;
        })) fail("unsupported WMB7 header dialect or padding");
    // The actual race world has no block/BSP geometry, embedded textures or lightmaps.
    for (const auto i : {1U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 11U, 13U, 16U, 17U, 18U}) {
        if (sections[i].length) fail("unsupported nonempty geometry/material section");
    }
    if (sections[2].length && (sections[2].length != 4 || file.u32(sections[2].offset) != 0))
        fail("unsupported embedded textures");
    WorldScene scene;
    if (sections[0].length) {
        if (sections[0].length != 784) fail("invalid palette boundary");
        std::copy_n(bytes.begin() + sections[0].offset + 16, 768, scene.palette.begin());
        scene.has_palette = true;
    }
    const auto objects = sections[15];
    if (!objects.length) fail("missing objects section");
    Reader list{bytes.subspan(objects.offset, objects.length)};
    const auto count = list.u32(0);
    if (!count || count > (objects.length - 4) / 4) fail("invalid object count");
    // One60-byte INFO and at least136 bytes per remaining supported entity,
    // plus four-byte offsets: reject impossible counts before reserving memory.
    if (objects.length < 68 || count > 1 + (objects.length - 68) / 140)
        fail("object count cannot fit supported record boundaries");
    const std::size_t table_end = 4 + std::size_t(count)*4;
    scene.object_count = count;
    scene.entities.reserve(count - 1);
    bool info = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto begin = list.u32(4 + std::size_t(i)*4);
        const auto end = i+1 < count ? list.u32(8 + std::size_t(i)*4) : objects.length;
        if (begin < table_end || begin >= end || end > objects.length)
            fail("invalid object offset or boundary");
        Reader record{list.bytes.subspan(begin, end - begin)};
        const auto type = record.u32(0);
        if (type == 5) {
            if (info || record.bytes.size() != 60) fail("invalid or duplicate level info");
            info = true;
            scene.sun_azimuth = record.number(16);
            scene.sun_elevation = record.number(20);
            scene.info_flags = record.u32(24);
            scene.compiler_version = record.number(28);
            scene.gamma = record.bytes[32];
            scene.lightmap_size_code = record.bytes[33];
            if (scene.lightmap_size_code > 2) fail("invalid lightmap size code");
            scene.sun_color = record.u32(36);
            scene.ambient_color = record.u32(40);
            for (std::size_t c = 0; c < 4; ++c) scene.fog_colors[c] = record.u32(44 + c*4);
            continue;
        }
        if (type != 3 && type != 7) fail("unsupported object type");
        if (record.bytes.size() != (type == 3 ? 136 : 372)) fail("invalid entity record boundary");
        WorldEntity entity;
        entity.object_index = i;
        entity.old_record = type == 3;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            entity.position[axis] = record.number(4 + axis*4);
            entity.angles[axis] = record.number(16 + axis*4);
            entity.scale[axis] = record.number(28 + axis*4);
        }
        if (type == 7) {
            entity.name = record.text(40, 33);
            entity.resource = record.text(73, 33);
            entity.action = record.text(106, 33);
            for (std::size_t s = 0; s < 20; ++s) entity.skills[s] = record.number(140 + s*4);
            entity.flags = record.u32(220);
            entity.ambient = record.number(224);
            entity.albedo = record.number(228);
            entity.path = record.u32(232);
            entity.attached_entity = record.u32(236);
            entity.material = record.text(240, 33);
            entity.string1 = record.text(273, 33);
            entity.string2 = record.text(306, 33);
            if (entity.path > count || entity.attached_entity > count) fail("invalid object attachment index");
        } else {
            entity.name = record.text(40, 20);
            entity.resource = record.text(60, 13);
            entity.action = record.text(73, 20);
            // Three original records overflow a long .MDL filename into an otherwise
            // empty action field. Preserve their actual NUL-terminated disk filename.
            if (record.bytes[72] != 0) {
                entity.resource = record.text(60, 33);
                entity.action.clear();
            }
            for (std::size_t s = 0; s < 8; ++s) entity.skills[s] = record.number(96 + s*4);
            entity.flags = record.u32(128);
            entity.ambient = record.number(132);
            // Original decoder1005c4d0 writes float50 from DLL constant1018f140.
            entity.albedo = 50;
        }
        if (!resource_name(entity.resource)) fail("unsafe or empty entity resource name");
        scene.entities.push_back(std::move(entity));
    }
    if (!info) fail("missing level info");
    // Source decoder: empty lists 5/11/17 leave worlddata counts +48/+70/+88 zero.
    // 1003bda0 clears roots 106ef520/524; 1008db70 creates them only for +70/+88 > 0.
    // This proves an empty world collision root, not absence of entity MDL collision.
    scene.source_empty_collision_root_known = true;
    return scene;
}

std::vector<std::vector<std::filesystem::path>> resolve_model_skins(
    const std::filesystem::path& owner, const Model& model, SourceProfile profile) {
    validate_profile(profile);
    return skin_paths(resource_catalog(owner, profile), model, profile);
}

std::vector<std::filesystem::path> resolve_resources(
    const std::filesystem::path& owner, std::span<const std::string_view> names, SourceProfile profile) {
    validate_profile(profile);
    const auto catalog = resource_catalog(owner, profile);
    std::vector<std::filesystem::path> paths;
    paths.reserve(names.size());
    for (const auto name : names) paths.push_back(resolve_source_resource(catalog, name, profile));
    return paths;
}

std::vector<WorldMaterialResources> resolve_material_resources(
    const std::filesystem::path& owner, std::span<const std::string_view> names, SourceProfile profile) {
    validate_profile(profile);
    const auto catalog = resource_catalog(owner, profile);
    std::vector<WorldMaterialResources> resources;
    resources.reserve(names.size());
    for (const auto name : names) resources.push_back(resolve_material(catalog, name, profile));
    return resources;
}

SourceMaterialIdentity source_resource_material_identity(
    const WorldModel& model, std::uint32_t source_flags, SourceProfile profile) {
    validate_profile(profile);
    const auto bitmap_format = [](ImageSourceFormat format) {
        return format == ImageSourceFormat::Bmp || format == ImageSourceFormat::Tga
            || format == ImageSourceFormat::Dds;
    };
    // Only original decoded resource provenance selects a source resource kind.
    // Skin material arrays and post-action/morphed GPU metadata are not ENTITY MATERIAL.
    const bool mdl = model.model && model.model->source_no_linked_brightness_known;
    const bool bitmap = (model.sprite_gpu && bitmap_format(model.sprite_gpu->source_format))
        || bitmap_format(model.sprite.source_format);
    if ((!mdl && !bitmap) || model.resource.empty()) return {};

    const auto path = model.resource.generic_string();
    std::string_view filename = path;
    const auto separator = filename.find_last_of("/\\");
    if (separator != std::string_view::npos) filename.remove_prefix(separator + 1);
    const auto member = filename.rfind("::");
    if (member != std::string_view::npos) filename.remove_prefix(member + 2);
    // Resource locators are not source API arguments. Only the compiled C4
    // bare-filename request cohort proves this filename reconstruction.
    if (!authored_bare_resource(filename)) return {};
    auto delimiter = filename.find('.');
    if (delimiter == std::string_view::npos) delimiter = filename.find('#');
    if (delimiter != std::string_view::npos) {
        // Fresh RESOURCE+60: bounded filename copy, first '.' (else '#') becomes '_'.
        std::array<char, 31> key{};
        const auto size = std::min(filename.size(), key.size());
        std::copy_n(filename.begin(), size, key.begin());
        if (delimiter < size) key[delimiter] = '_';
        if (const auto registered = registered_material({key.data(), size}, profile); !registered.empty())
            return {registered, true};
    }
    // RESOURCE+60 wins before the live public flags; source sprites win before METAL.
    const auto builtin = (source_flags & 0x20000U) ? "mat_unlit"
        : bitmap ? "mat_sprite"
        : (source_flags & 0x400000U) ? "mat_metal" : "mat_model";
    return {registered_material(builtin, profile), true};
}

SourceMaterialIdentity source_fresh_material_identity(const WorldModel& model, SourceProfile profile) {
    // WMB attachment uses public flags zero, independently of later authored flags.
    return source_resource_material_identity(model, 0, profile);
}

std::array<float, 16> world_entity_transform(const WorldEntity& entity) {
    // Original acknex.dll 100421f0 has these nine coefficients (Rz*Ry(-tilt)*Rx).
    // http://manual.conitec.net/ang_to_matrix.htm corroborates the Y/Z conversion.
    constexpr double radians = 3.14159265358979323846 / 180.0;
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (!std::isfinite(entity.position[axis]) || !std::isfinite(entity.angles[axis])
            || !std::isfinite(entity.scale[axis])) fail("nonfinite entity transform");
    const auto cp = std::cos(entity.angles[0]*radians), sp = std::sin(entity.angles[0]*radians);
    const auto ct = std::cos(entity.angles[1]*radians), st = std::sin(entity.angles[1]*radians);
    const auto cr = std::cos(entity.angles[2]*radians), sr = std::sin(entity.angles[2]*radians);
    const auto sx = entity.scale[0], sy = entity.scale[1], sz = entity.scale[2];
    std::array<float, 16> matrix{
        float(cp*ct*sx), float(sp*ct*sx), float(st*sx), 0,
        float((-cp*st*sr-sp*cr)*sy), float((-sp*st*sr+cp*cr)*sy), float(ct*sr*sy), 0,
        float((-cp*st*cr+sp*sr)*sz), float((-sp*st*cr-cp*sr)*sz), float(ct*cr*sz), 0,
        entity.position[0], entity.position[1], entity.position[2], 1
    };
    if (std::any_of(matrix.begin(), matrix.end(), [](float value) { return !std::isfinite(value); }))
        fail("entity transform overflow");
    return matrix;
}

WorldScene load_world(const std::filesystem::path& path, SourceProfile profile,
                      const GpuImageUploadOptions& sprite_upload) {
    validate_profile(profile);
    auto scene = parse_world(read_resource(path));
    scene.source_profile = profile;
    const auto catalog = resource_catalog(path, profile);
    // Entity strings remain stable throughout this loop; other names refer to static tables.
    using Key = std::array<std::string_view, 5>;
    std::map<Key, std::uint32_t> cache;
    std::map<std::string_view, SourceMaterialIdentity> initial_materials;
    std::map<std::string, std::string> unsupported;
    std::map<std::filesystem::path, std::shared_ptr<const Model>> geometry;
    const auto shared_model = [&geometry](const std::filesystem::path& resource) {
        const auto found = geometry.find(resource);
        if (found != geometry.end()) return found->second;
        auto model = std::make_shared<const Model>(load_model(resource));
        geometry.emplace(resource, model);
        return model;
    };
    const auto load_original = [&](std::string_view name, bool mdl) {
        WorldModel model;
        model.resource = resolve_source_resource(catalog, name, profile);
        if (mdl) model.model = shared_model(model.resource);
        else model.sprite_gpu = load_gpu_image(model.resource, model.sprite, sprite_upload);
        return model;
    };
    for (auto& entity : scene.entities) {
        const auto extension = lowercase(std::filesystem::path(entity.resource).extension().string());
        WorldModel model;
        auto initial = initial_materials.find(entity.resource);
        if (initial == initial_materials.end()) {
            model = load_original(entity.resource, extension == ".mdl");
            initial = initial_materials.emplace(entity.resource,
                source_fresh_material_identity(model, profile)).first;
        }
        auto identity = initial->second;
        // Editor lookup overwrites current MATERIAL even on a miss (actual NULL).
        if (!entity.material.empty() && !equal_material_name(entity.material, "ndef"))
            identity = {registered_material(entity.material, profile), true};
        entity.source_initial_material_identity = identity.identity;
        entity.source_initial_material_known = identity.known;
        // Cold normal ENTITY+3a4 is independently NULL; mode3 does not commit it.
        entity.source_previous_material_identity = {};
        entity.source_previous_material_known = true;
        const auto binding = action_binding(entity, profile);
        if(std::find(material_before_wait_actions.begin(),material_before_wait_actions.end(),entity.action)
            !=material_before_wait_actions.end())
            entity.source_action_material_before_wait=binding.material;
        entity.source_flags_set = binding.flags_set;
        entity.source_flags_clear = binding.flags_clear;
        const auto appearance = action_appearance(entity.action);
        if (appearance.alpha) entity.alpha_percent = *appearance.alpha;
        entity.source_ambient = appearance.ambient;
        entity.source_albedo = appearance.albedo;
        const std::string_view material_key = binding.material.empty()
            ? std::string_view(entity.material) : binding.material;
        if (!binding.unsupported.empty())
            unsupported.try_emplace(entity.action, binding.unsupported);
        const auto effective_flags = (entity.flags | binding.flags_set) & ~binding.flags_clear;
        if (extension != ".mdl" && ((entity.flags | effective_flags) & 0x8000U))
            fail("unsupported original sprite texture mode 16 (authored flag 0x8000)");
        Key key{entity.resource, binding.model, binding.imported, binding.texture, material_key};
        const auto found = cache.find(key);
        if (found != cache.end()) {
            entity.model_index = found->second;
            continue;
        }
        if (model.resource.empty()) model = load_original(entity.resource, extension == ".mdl");
        if (!binding.model.empty()) {
            model.morphed_resource = resolve_source_resource(catalog, binding.model, profile);
            model.morphed_model = shared_model(model.morphed_resource);
        }
        const auto& rendered = model.morphed_model ? model.morphed_model : model.model;
        if (rendered) model.skin_resources = skin_paths(catalog, *rendered, profile);
        if (!binding.imported.empty()) {
            model.imported_resource = resolve_source_resource(catalog, binding.imported, profile);
            model.imported_mesh = load_mesh(model.imported_resource);
            // Original plugin supplies D3D mesh coordinates. Independent ana_yol.MDL
            // vs sh_ay.rc4 bounds establish exactly X,Z,Y, including normals.
            for (auto& vertex : model.imported_mesh.vertices) {
                std::swap(vertex.y, vertex.z);
                std::swap(vertex.ny, vertex.nz);
            }
        }
        if (!binding.texture.empty())
            model.texture_resource = resolve_source_resource(catalog, binding.texture, profile);
        model.material = material_key;
        if (!model.material.empty()) {
            auto material = resolve_material(catalog, model.material, profile);
            model.material_skin_resources = std::move(material.skins);
            model.material_skins_known_mask = material.material_skins_known_mask;
            model.material_skin_cube_mask = material.material_skin_cube_mask;
            model.effect_resource = std::move(material.effect);
            if (!material.definition_known) {
                const auto note = model.material + ": original active material definition is absent";
                if (std::find(scene.unresolved_materials.begin(), scene.unresolved_materials.end(), note)
                    == scene.unresolved_materials.end()) scene.unresolved_materials.push_back(note);
            }
            if (!model.effect_resource.empty() && (model.material == "Mtl_tabela_8_9"
                || model.material == "Mtl_33" || model.material == "Mtl_kapsul1")) {
                const auto note = model.material + ": original Normal_Optim normal-slot assignment is commented out";
                if (std::find(scene.unresolved_materials.begin(), scene.unresolved_materials.end(), note)
                    == scene.unresolved_materials.end()) scene.unresolved_materials.push_back(note);
            }
        }
        entity.model_index = static_cast<std::uint32_t>(scene.models.size());
        cache.emplace(std::move(key), entity.model_index);
        scene.models.push_back(std::move(model));
    }
    scene.unresolved_actions.reserve(unsupported.size());
    for (const auto& [action, reason] : unsupported)
        scene.unresolved_actions.push_back(action + ": " + reason);
    return scene;
}
} // namespace c4::assets
